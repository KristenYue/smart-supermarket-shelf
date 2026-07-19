package com.smartshelf;

import software.amazon.awssdk.services.dynamodb.DynamoDbClient;
import software.amazon.awssdk.services.dynamodb.model.AttributeValue;
import software.amazon.awssdk.services.dynamodb.model.PutItemRequest;
import software.amazon.awssdk.services.dynamodb.model.QueryRequest;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

final class DynamoRepository {
    private final DynamoDbClient dynamo;
    private final Config config;

    DynamoRepository(DynamoDbClient dynamo, Config config) {
        this.dynamo = dynamo;
        this.config = config;
    }

    List<Map<String, Object>> latestTelemetry(String deviceId, int limit) {
        return query(config.telemetryTable(), deviceId, limit);
    }

    List<Map<String, Object>> latestAnalysis(String deviceId, int limit) {
        return query(config.analysisTable(), deviceId, limit);
    }

    private List<Map<String, Object>> query(String table, String deviceId, int requestedLimit) {
        int limit = Math.max(1, Math.min(requestedLimit, 100));
        QueryRequest request = QueryRequest.builder()
                .tableName(table)
                .keyConditionExpression("deviceID = :device")
                .expressionAttributeValues(Map.of(
                        ":device", AttributeValue.builder().s(deviceId).build()))
                .scanIndexForward(false)
                .limit(limit)
                .build();

        List<Map<String, Object>> output = new ArrayList<>();
        for (Map<String, AttributeValue> item : dynamo.query(request).items()) {
            output.add(fromItem(item));
        }
        return output;
    }

    void putAnalysis(Map<String, Object> value) {
        Map<String, AttributeValue> item = new LinkedHashMap<>();
        value.forEach((key, field) -> {
            AttributeValue attribute = toAttribute(field);
            if (attribute != null) item.put(key, attribute);
        });
        dynamo.putItem(PutItemRequest.builder()
                .tableName(config.analysisTable())
                .item(item)
                .build());
    }

    private static Map<String, Object> fromItem(Map<String, AttributeValue> item) {
        Map<String, Object> output = new LinkedHashMap<>();
        item.forEach((key, value) -> output.put(key, fromAttribute(value)));
        return output;
    }

    private static Object fromAttribute(AttributeValue value) {
        if (value.s() != null) return value.s();
        if (value.n() != null) {
            try {
                return value.n().contains(".") ? Double.parseDouble(value.n()) : Long.parseLong(value.n());
            } catch (NumberFormatException ignored) {
                return value.n();
            }
        }
        if (value.bool() != null) return value.bool();
        if (Boolean.TRUE.equals(value.nul())) return null;
        if (value.hasL()) return value.l().stream().map(DynamoRepository::fromAttribute).toList();
        if (value.hasM()) return fromItem(value.m());
        return null;
    }

    private static AttributeValue toAttribute(Object value) {
        if (value == null) return AttributeValue.builder().nul(true).build();
        if (value instanceof Boolean b) return AttributeValue.builder().bool(b).build();
        if (value instanceof Number n) return AttributeValue.builder().n(n.toString()).build();
        return AttributeValue.builder().s(value.toString()).build();
    }
}

