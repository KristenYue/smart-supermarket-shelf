package com.smartshelf.alerts;

import java.util.List;
import java.util.Map;

public interface ShelfSource {
    List<Map<String, Object>> analysis(String deviceId, int limit);
    List<Map<String, Object>> telemetry(String deviceId, int limit);
}
