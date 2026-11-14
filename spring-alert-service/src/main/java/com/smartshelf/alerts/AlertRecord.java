package com.smartshelf.alerts;

public record AlertRecord(
        long id,
        String deviceId,
        long sourceTimestamp,
        String state,
        String advice) {}
