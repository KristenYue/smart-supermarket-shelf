package com.smartshelf.alerts;

import java.util.List;
import java.util.Map;

import org.springframework.http.HttpStatus;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.ResponseStatus;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.server.ResponseStatusException;
import org.springframework.web.client.RestClientException;

@RestController
@RequestMapping("/api/devices/{deviceId}")
public class DeviceController {
    private final ShelfSource source;
    private final AlertStore store;

    DeviceController(ShelfSource source, AlertStore store) {
        this.source = source;
        this.store = store;
    }

    @GetMapping("/telemetry")
    public List<Map<String, Object>> telemetry(@PathVariable String deviceId,
                                                @RequestParam(defaultValue = "50") int limit) {
        requireKnownDevice(deviceId);
        try {
            return source.telemetry(deviceId, bounded(limit));
        } catch (RestClientException error) {
            throw new ResponseStatusException(HttpStatus.BAD_GATEWAY,
                    "Existing shelf service is unavailable", error);
        }
    }

    @GetMapping("/alerts")
    public List<AlertRecord> alerts(@PathVariable String deviceId,
                                     @RequestParam(defaultValue = "50") int limit) {
        requireKnownDevice(deviceId);
        return store.recent(deviceId, bounded(limit));
    }

    private static int bounded(int limit) {
        if (limit < 1 || limit > 100) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "limit must be 1..100");
        }
        return limit;
    }

    private static void requireKnownDevice(String deviceId) {
        if (!AlertSyncService.DEVICE_IDS.contains(deviceId)) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Unknown device");
        }
    }
}
