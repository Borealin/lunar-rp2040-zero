package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"math"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	protocolVersion = "LS1"
	flagDetected    = 0x01
	flagValid       = 0x02
	flagFresh       = 0x04
)

var bridgeVersion = "dev"

type sample struct {
	Sequence      uint32    `json:"sequence"`
	DeviceUptime  uint32    `json:"device_uptime_ms"`
	Lux           float64   `json:"lux"`
	RawFull       uint16    `json:"raw_full"`
	RawIR         uint16    `json:"raw_ir"`
	Gain          uint16    `json:"gain"`
	IntegrationMS uint16    `json:"integration_ms"`
	Flags         uint8     `json:"flags"`
	Errors        uint32    `json:"errors"`
	ReceivedAt    time.Time `json:"-"`
}

func (s sample) detected() bool { return s.Flags&flagDetected != 0 }
func (s sample) valid() bool    { return s.Flags&flagValid != 0 }
func (s sample) deviceFresh() bool {
	return s.Flags&flagFresh != 0
}

type device struct {
	requestedPath string
	debug         bool

	connectionMu sync.Mutex
	connection   *os.File
	path         string

	sampleMu sync.RWMutex
	latest   sample
	hasValue bool

	nonce atomic.Uint32
}

func crc16CCITT(data string) uint16 {
	crc := uint16(0xffff)
	for i := 0; i < len(data); i++ {
		crc ^= uint16(data[i]) << 8
		for bit := 0; bit < 8; bit++ {
			if crc&0x8000 != 0 {
				crc = (crc << 1) ^ 0x1021
			} else {
				crc <<= 1
			}
		}
	}
	return crc
}

func makeFrame(payload string) string {
	return fmt.Sprintf("@%s*%04X\r\n", payload, crc16CCITT(payload))
}

func parseFrame(line string) (string, error) {
	line = strings.TrimSpace(line)
	if len(line) < 7 || line[0] != '@' {
		return "", errors.New("missing frame marker")
	}
	separator := strings.LastIndexByte(line, '*')
	if separator < 2 || len(line)-separator-1 != 4 {
		return "", errors.New("invalid checksum field")
	}
	received, err := strconv.ParseUint(line[separator+1:], 16, 16)
	if err != nil {
		return "", fmt.Errorf("invalid checksum: %w", err)
	}
	payload := line[1:separator]
	if crc16CCITT(payload) != uint16(received) {
		return "", errors.New("checksum mismatch")
	}
	return payload, nil
}

func parseUint(field string, bits int) (uint64, error) {
	value, err := strconv.ParseUint(field, 10, bits)
	if err != nil {
		return 0, fmt.Errorf("invalid integer %q: %w", field, err)
	}
	return value, nil
}

func parseSample(payload string) (sample, error) {
	fields := strings.Split(payload, ",")
	if len(fields) != 11 || fields[0] != protocolVersion || fields[1] != "S" {
		return sample{}, errors.New("not an LS1 sensor frame")
	}
	values := make([]uint64, 9)
	bits := []int{32, 32, 32, 16, 16, 16, 16, 8, 32}
	for i := range values {
		value, err := parseUint(fields[i+2], bits[i])
		if err != nil {
			return sample{}, err
		}
		values[i] = value
	}
	return sample{
		Sequence:      uint32(values[0]),
		DeviceUptime:  uint32(values[1]),
		Lux:           float64(values[2]) / 1000.0,
		RawFull:       uint16(values[3]),
		RawIR:         uint16(values[4]),
		Gain:          uint16(values[5]),
		IntegrationMS: uint16(values[6]),
		Flags:         uint8(values[7]),
		Errors:        uint32(values[8]),
		ReceivedAt:    time.Now(),
	}, nil
}

func serialCandidates(requested string) ([]string, error) {
	if requested != "" {
		return []string{requested}, nil
	}
	paths, err := filepath.Glob("/dev/cu.usbmodem*")
	if err != nil {
		return nil, err
	}
	type candidate struct {
		path    string
		modTime time.Time
	}
	candidates := make([]candidate, 0, len(paths))
	for _, path := range paths {
		info, statErr := os.Stat(path)
		if statErr == nil {
			candidates = append(candidates, candidate{path: path, modTime: info.ModTime()})
		}
	}
	sort.Slice(candidates, func(i, j int) bool {
		return candidates[i].modTime.After(candidates[j].modTime)
	})
	result := make([]string, 0, len(candidates))
	for _, candidate := range candidates {
		result = append(result, candidate.path)
	}
	return result, nil
}

func configureSerial(path string) error {
	command := exec.Command("/bin/stty", "-f", path, "115200", "raw", "-echo")
	if output, err := command.CombinedOutput(); err != nil {
		return fmt.Errorf("stty %s: %w: %s", path, err, strings.TrimSpace(string(output)))
	}
	return nil
}

func (d *device) setConnection(file *os.File, path string) {
	d.connectionMu.Lock()
	d.connection = file
	d.path = path
	d.connectionMu.Unlock()
}

func (d *device) clearConnection(file *os.File) {
	d.connectionMu.Lock()
	if d.connection == file {
		d.connection = nil
		d.path = ""
	}
	d.connectionMu.Unlock()
}

func (d *device) connectionPath() string {
	d.connectionMu.Lock()
	defer d.connectionMu.Unlock()
	return d.path
}

func (d *device) run(ctx context.Context) {
	for ctx.Err() == nil {
		paths, err := serialCandidates(d.requestedPath)
		if err != nil {
			log.Printf("serial discovery: %v", err)
		}
		connected := false
		for _, path := range paths {
			if ctx.Err() != nil {
				return
			}
			if err := configureSerial(path); err != nil {
				if d.debug {
					log.Printf("skip %s: %v", path, err)
				}
				continue
			}
			file, openErr := os.OpenFile(path, os.O_RDWR, 0)
			if openErr != nil {
				if d.debug {
					log.Printf("open %s: %v", path, openErr)
				}
				continue
			}
			connected = true
			d.setConnection(file, path)
			log.Printf("CDC connected: %s", path)
			d.readLoop(file)
			d.clearConnection(file)
			_ = file.Close()
			log.Printf("CDC disconnected: %s", path)
			break
		}
		if !connected && d.debug {
			log.Printf("waiting for Lunar RP2040 CDC device")
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(time.Second):
		}
	}
}

func (d *device) readLoop(file *os.File) {
	scanner := bufio.NewScanner(file)
	scanner.Buffer(make([]byte, 256), 1024)
	for scanner.Scan() {
		payload, err := parseFrame(scanner.Text())
		if err != nil {
			if d.debug {
				log.Printf("discard CDC frame: %v", err)
			}
			continue
		}
		if strings.HasPrefix(payload, protocolVersion+",S,") {
			value, parseErr := parseSample(payload)
			if parseErr != nil {
				if d.debug {
					log.Printf("discard sensor frame: %v", parseErr)
				}
				continue
			}
			d.sampleMu.Lock()
			d.latest = value
			d.hasValue = true
			d.sampleMu.Unlock()
			if d.debug {
				log.Printf("sample seq=%d lux=%.3f flags=0x%02x", value.Sequence, value.Lux, value.Flags)
			}
		} else if d.debug {
			log.Printf("CDC %s", payload)
		}
	}
	if err := scanner.Err(); err != nil && d.debug {
		log.Printf("serial read: %v", err)
	}
}

func (d *device) current(maxAge time.Duration) (sample, bool) {
	value, ok := d.snapshot()
	return value, ok && value.deviceFresh() && value.valid() &&
		time.Since(value.ReceivedAt) <= maxAge && !math.IsNaN(value.Lux) && !math.IsInf(value.Lux, 0)
}

func (d *device) snapshot() (sample, bool) {
	d.sampleMu.RLock()
	value, ok := d.latest, d.hasValue
	d.sampleMu.RUnlock()
	return value, ok
}

func (d *device) sendBootsel() (uint32, error) {
	nonce := d.nonce.Add(1)
	payload := fmt.Sprintf("%s,C,%d,BOOTSEL", protocolVersion, nonce)
	frame := []byte(makeFrame(payload))

	d.connectionMu.Lock()
	defer d.connectionMu.Unlock()
	if d.connection == nil {
		return 0, errors.New("CDC device is not connected")
	}
	if _, err := d.connection.Write(frame); err != nil {
		return 0, fmt.Errorf("write BOOTSEL command: %w", err)
	}
	return nonce, nil
}

type apiServer struct {
	device *device
	maxAge time.Duration
}

func writeJSON(response http.ResponseWriter, status int, body any) {
	response.Header().Set("Content-Type", "application/json")
	response.Header().Set("Cache-Control", "no-store")
	response.Header().Set("Access-Control-Allow-Origin", "*")
	response.WriteHeader(status)
	_ = json.NewEncoder(response).Encode(body)
}

func lunarState(value sample) map[string]any {
	return map[string]any{
		"id":    "sensor-ambient_light",
		"state": fmt.Sprintf("%.3f lx", value.Lux),
		"value": value.Lux,
	}
}

func (server *apiServer) sensor(response http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		writeJSON(response, http.StatusMethodNotAllowed, map[string]string{"error": "use GET"})
		return
	}
	value, fresh := server.device.current(server.maxAge)
	if !fresh {
		writeJSON(response, http.StatusServiceUnavailable,
			map[string]string{"error": "no ambient light reading available"})
		return
	}
	writeJSON(response, http.StatusOK, lunarState(value))
}

func (server *apiServer) health(response http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		writeJSON(response, http.StatusMethodNotAllowed, map[string]string{"error": "use GET"})
		return
	}
	value, hasValue := server.device.snapshot()
	_, fresh := server.device.current(server.maxAge)
	body := map[string]any{
		"status":           "degraded",
		"protocol":         protocolVersion,
		"serial_connected": server.device.connectionPath() != "",
		"serial_path":      server.device.connectionPath(),
		"reading_fresh":    fresh,
	}
	if hasValue {
		body["sensor_detected"] = value.detected()
		body["lux"] = value.Lux
		body["age_ms"] = time.Since(value.ReceivedAt).Milliseconds()
		body["gain"] = value.Gain
		body["integration_ms"] = value.IntegrationMS
		body["raw_full"] = value.RawFull
		body["raw_ir"] = value.RawIR
		body["errors"] = value.Errors
	}
	status := http.StatusServiceUnavailable
	if fresh {
		body["status"] = "ok"
		status = http.StatusOK
	}
	writeJSON(response, status, body)
}

func (server *apiServer) events(response http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		writeJSON(response, http.StatusMethodNotAllowed, map[string]string{"error": "use GET"})
		return
	}
	flusher, ok := response.(http.Flusher)
	if !ok {
		writeJSON(response, http.StatusInternalServerError, map[string]string{"error": "streaming unavailable"})
		return
	}
	response.Header().Set("Content-Type", "text/event-stream")
	response.Header().Set("Cache-Control", "no-cache")
	response.Header().Set("Connection", "keep-alive")
	response.Header().Set("Access-Control-Allow-Origin", "*")
	response.WriteHeader(http.StatusOK)
	flusher.Flush()

	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-request.Context().Done():
			return
		case <-ticker.C:
			value, fresh := server.device.current(server.maxAge)
			if !fresh {
				continue
			}
			encoded, _ := json.Marshal(lunarState(value))
			if _, err := fmt.Fprintf(response, "event: state\ndata: %s\n\n", encoded); err != nil {
				return
			}
			flusher.Flush()
		}
	}
}

func (server *apiServer) bootloader(response http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodPost {
		writeJSON(response, http.StatusMethodNotAllowed, map[string]string{"error": "use POST"})
		return
	}
	if origin := request.Header.Get("Origin"); origin != "" &&
		origin != "http://127.0.0.1:4765" && origin != "http://localhost:4765" {
		writeJSON(response, http.StatusForbidden,
			map[string]string{"error": "cross-origin bootloader request denied"})
		return
	}
	nonce, err := server.device.sendBootsel()
	if err != nil {
		writeJSON(response, http.StatusServiceUnavailable, map[string]string{"error": err.Error()})
		return
	}
	writeJSON(response, http.StatusAccepted, map[string]any{
		"status": "BOOTSEL command sent over CDC",
		"nonce":  nonce,
	})
}

func (server *apiServer) root(response http.ResponseWriter, request *http.Request) {
	if request.URL.Path != "/" {
		writeJSON(response, http.StatusNotFound, map[string]string{"error": "not found"})
		return
	}
	writeJSON(response, http.StatusOK, map[string]any{
		"name":     "Lunar RP2040 Sensor Bridge",
		"version":  bridgeVersion,
		"protocol": protocolVersion,
		"sensor":   "/sensor/ambient_light",
		"events":   "/events",
	})
}

func main() {
	listen := flag.String("listen", "127.0.0.1:4765", "HTTP listen address")
	serialPath := flag.String("serial", "", "CDC path; auto-detect /dev/cu.usbmodem* when empty")
	staleAfter := flag.Duration("stale-after", 5*time.Second, "maximum reading age")
	debug := flag.Bool("debug", false, "enable protocol diagnostics")
	bootsel := flag.Bool("bootsel", false, "send the CDC BOOTSEL command and exit")
	showVersion := flag.Bool("version", false, "print version and exit")
	flag.Parse()
	if *showVersion {
		fmt.Println(bridgeVersion)
		return
	}

	dev := &device{requestedPath: *serialPath, debug: *debug}
	server := &apiServer{device: dev, maxAge: *staleAfter}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go dev.run(ctx)
	if *bootsel {
		deadline := time.Now().Add(8 * time.Second)
		for dev.connectionPath() == "" && time.Now().Before(deadline) {
			time.Sleep(100 * time.Millisecond)
		}
		nonce, err := dev.sendBootsel()
		if err != nil {
			log.Fatal(err)
		}
		log.Printf("CDC BOOTSEL command sent, nonce=%d", nonce)
		time.Sleep(time.Second)
		return
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/", server.root)
	mux.HandleFunc("/sensor/ambient_light", server.sensor)
	mux.HandleFunc("/events", server.events)
	mux.HandleFunc("/healthz", server.health)
	mux.HandleFunc("/system/bootloader", server.bootloader)

	log.Printf("Lunar HTTP bridge listening on http://%s", *listen)
	httpServer := &http.Server{
		Addr:              *listen,
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
		IdleTimeout:       30 * time.Second,
	}
	if err := httpServer.ListenAndServe(); err != nil {
		log.Fatal(err)
	}
}
