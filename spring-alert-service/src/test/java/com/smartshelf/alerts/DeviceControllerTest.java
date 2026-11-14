package com.smartshelf.alerts;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.junit.jupiter.api.Test;
import org.springframework.http.HttpStatus;
import org.springframework.web.client.ResourceAccessException;
import org.springframework.web.server.ResponseStatusException;

class DeviceControllerTest {
    @Test
    void queriesOnlyTheRequestedDevice() {
        AlertRecord row = new AlertRecord(1, "smart-shelf-01", 100, "ALERT_ENV", "Inspect");
        RecordingStore store = new RecordingStore(List.of(row));
        DeviceController controller = new DeviceController(new EmptySource(), store);

        assertEquals(List.of(row), controller.alerts("smart-shelf-01", 20));
        assertEquals(List.of("smart-shelf-01:20"), store.queries);
    }

    @Test
    void rejectsUnknownDeviceAndUnboundedLimit() {
        RecordingStore store = new RecordingStore(List.of());
        DeviceController controller = new DeviceController(new EmptySource(), store);
        ResponseStatusException unknown = assertThrows(ResponseStatusException.class,
                () -> controller.alerts("other-device", 20));
        assertEquals(HttpStatus.BAD_REQUEST, unknown.getStatusCode());
        assertThrows(ResponseStatusException.class,
                () -> controller.alerts("smart-shelf-01", 101));
        assertEquals(List.of(), store.queries);
    }

    @Test
    void reportsSourceFailureAsBadGateway() {
        ShelfSource failingSource = new EmptySource() {
            @Override
            public List<Map<String, Object>> telemetry(String deviceId, int limit) {
                throw new ResourceAccessException("connection refused");
            }
        };
        DeviceController controller = new DeviceController(
                failingSource, new RecordingStore(List.of()));
        ResponseStatusException error = assertThrows(ResponseStatusException.class,
                () -> controller.telemetry("smart-shelf-01", 10));
        assertEquals(HttpStatus.BAD_GATEWAY, error.getStatusCode());
    }

    private static class EmptySource implements ShelfSource {
        @Override
        public List<Map<String, Object>> analysis(String deviceId, int limit) {
            return List.of();
        }

        @Override
        public List<Map<String, Object>> telemetry(String deviceId, int limit) {
            return List.of();
        }
    }

    private static final class RecordingStore implements AlertStore {
        private final List<AlertRecord> rows;
        private final List<String> queries = new ArrayList<>();

        private RecordingStore(List<AlertRecord> rows) {
            this.rows = rows;
        }

        @Override
        public void save(String deviceId, long sourceTimestamp, String state, String advice) {
        }

        @Override
        public List<AlertRecord> recent(String deviceId, int limit) {
            queries.add(deviceId + ":" + limit);
            return rows;
        }
    }
}
