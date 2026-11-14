package com.smartshelf.alerts;

import java.util.List;

public interface AlertStore {
    void save(String deviceId, long sourceTimestamp, String state, String advice);
    List<AlertRecord> recent(String deviceId, int limit);
}
