package main

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"strconv"
	"strings"
	"testing"
	"time"
)

func TestFrameRoundTrip(t *testing.T) {
	payload := "LS1,C,42,BOOTSEL"
	frame := makeFrame(payload)
	parsed, err := parseFrame(frame)
	if err != nil {
		t.Fatalf("parseFrame: %v", err)
	}
	if parsed != payload {
		t.Fatalf("payload = %q, want %q", parsed, payload)
	}
}

func TestBootselCommandFrame(t *testing.T) {
	reader, writer, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	defer reader.Close()

	dev := &device{connection: writer}
	nonce, err := dev.sendBootsel()
	if err != nil {
		t.Fatalf("sendBootsel: %v", err)
	}
	_ = writer.Close()
	frame, err := io.ReadAll(reader)
	if err != nil {
		t.Fatal(err)
	}
	payload, err := parseFrame(string(frame))
	if err != nil {
		t.Fatalf("parseFrame: %v", err)
	}
	want := "LS1,C," + strconv.FormatUint(uint64(nonce), 10) + ",BOOTSEL"
	if payload != want {
		t.Fatalf("payload = %q, want %q", payload, want)
	}
}

func TestFrameRejectsCorruption(t *testing.T) {
	frame := makeFrame("LS1,C,42,BOOTSEL")
	frame = strings.Replace(frame, "BOOTSEL", "BO0TSEL", 1)
	if _, err := parseFrame(frame); err == nil {
		t.Fatal("parseFrame accepted a corrupted frame")
	}
}

func TestParseSample(t *testing.T) {
	value, err := parseSample("LS1,S,7,12345,91234,1000,120,25,200,7,3")
	if err != nil {
		t.Fatalf("parseSample: %v", err)
	}
	if value.Sequence != 7 || value.Lux != 91.234 || value.RawFull != 1000 ||
		value.RawIR != 120 || value.Gain != 25 || value.IntegrationMS != 200 ||
		!value.detected() || !value.valid() || !value.deviceFresh() || value.Errors != 3 {
		t.Fatalf("unexpected sample: %+v", value)
	}
}

func TestSensorHTTPContract(t *testing.T) {
	dev := &device{
		latest: sample{
			Lux:        91.234,
			Flags:      flagDetected | flagValid | flagFresh,
			ReceivedAt: time.Now(),
		},
		hasValue: true,
	}
	server := &apiServer{device: dev, maxAge: 5 * time.Second}
	response := httptest.NewRecorder()
	server.sensor(response, httptest.NewRequest(http.MethodGet, "/sensor/ambient_light", nil))
	if response.Code != http.StatusOK {
		t.Fatalf("status = %d, body = %s", response.Code, response.Body.String())
	}
	var body map[string]any
	if err := json.Unmarshal(response.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if body["id"] != "sensor-ambient_light" || body["value"] != 91.234 {
		t.Fatalf("unexpected Lunar response: %#v", body)
	}
}

func TestStaleReadingIsNotPublished(t *testing.T) {
	dev := &device{
		latest: sample{
			Lux:        12.0,
			Flags:      flagDetected | flagValid | flagFresh,
			ReceivedAt: time.Now().Add(-6 * time.Second),
		},
		hasValue: true,
	}
	server := &apiServer{device: dev, maxAge: 5 * time.Second}
	response := httptest.NewRecorder()
	server.sensor(response, httptest.NewRequest(http.MethodGet, "/sensor/ambient_light", nil))
	if response.Code != http.StatusServiceUnavailable {
		t.Fatalf("status = %d, want 503", response.Code)
	}
}
