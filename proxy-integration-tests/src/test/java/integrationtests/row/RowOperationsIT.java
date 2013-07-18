/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 * Copyright 2013 Near Infinity Corporation.
 */


package integrationtests.row;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.primitives.Longs;
import com.nearinfinity.honeycomb.config.AdapterType;
import com.nearinfinity.honeycomb.mysql.HandlerProxy;
import com.nearinfinity.honeycomb.mysql.QueryKey;
import com.nearinfinity.honeycomb.mysql.Row;
import com.nearinfinity.honeycomb.mysql.Util;
import com.nearinfinity.honeycomb.mysql.gen.ColumnType;
import com.nearinfinity.honeycomb.mysql.gen.QueryType;
import com.nearinfinity.honeycomb.mysql.schema.ColumnSchema;
import com.nearinfinity.honeycomb.mysql.schema.IndexSchema;
import com.nearinfinity.honeycomb.mysql.schema.TableSchema;
import integrationtests.HoneycombIntegrationTest;
import integrationtests.ITUtils;
import integrationtests.TestConstants;
import org.junit.Test;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static org.hamcrest.core.IsEqual.equalTo;
import static org.junit.Assert.*;

public class RowOperationsIT extends HoneycombIntegrationTest {

    private static final int ROW_COUNT = 1;
    private static final int INDEX_COL_VALUE = 7;
    private static final Map<String, ByteBuffer> fields = ImmutableMap.of(
            TestConstants.COLUMN1,
            ByteBuffer.wrap(Longs.toByteArray(1)),
            TestConstants.COLUMN2,
            ByteBuffer.wrap(Longs.toByteArray(2)));

    @Test
    public void testInsertRows() {
        final long numRows = 13;
        new InsertRows(numRows).run(proxy);
        ITUtils.assertRowCount(proxy, getTableSchema(), numRows);
    }

    @Test
    public void testInsertRowsConcurrently() throws Exception {
        final long numRows = 13;
        final int concurrency = 8;
        final long expectedRowCount = numRows * concurrency;
        ITUtils.startProxyActionConcurrently(concurrency,
                ITUtils.openTable,
                new InsertRows(numRows),
                ITUtils.closeTable,
                factory);
        ITUtils.assertRowCount(proxy, getTableSchema(), expectedRowCount);
    }

    @Test
    public void testGetRow() {
        ITUtils.insertData(proxy, ROW_COUNT, INDEX_COL_VALUE);

        final QueryKey key = ITUtils.createKey(INDEX_COL_VALUE, QueryType.EXACT_KEY);
        proxy.startIndexScan(key.serialize());

        final Row r = getDeserialize(proxy.getNextRow());
        final byte[] result = proxy.getRow(Util.UUIDToBytes(r.getUUID()));

        assertNotNull(result);
        assertThat(getDeserialize(result).getUUID(), equalTo(r.getUUID()));
    }

    @Test
    public void testDeleteRow() {
        final Map<String, ByteBuffer> map = Maps.newHashMap();
        map.put(TestConstants.COLUMN1, ITUtils.encodeValue(INDEX_COL_VALUE));
        map.put(TestConstants.COLUMN2, ITUtils.encodeValue(6));

        final Row row = getRow(map);
        proxy.insertRow(row.serialize());
        proxy.flush();

        final QueryKey key = ITUtils.createKey(INDEX_COL_VALUE, QueryType.EXACT_KEY);

        // Verify that there are index rows
        ITUtils.assertReceivingDifferentRows(proxy, key, 1);

        proxy.startIndexScan(key.serialize());
        proxy.deleteRow(proxy.getNextRow());
        proxy.flush();

        // Verify that no index rows remain
        ITUtils.assertReceivingDifferentRows(proxy, key, 0);
    }

    @Test
    public void testDeleteAllRows() {
        final QueryKey key = ITUtils.createKey(INDEX_COL_VALUE, QueryType.EXACT_KEY);

        // Add an indexed data row
        ITUtils.insertData(proxy, ROW_COUNT, INDEX_COL_VALUE);

        proxy.deleteAllRows();

        // Verify that no data rows remain
        ITUtils.assertReceivingDifferentRows(proxy, 0);

        // Verify that no index rows remain
        ITUtils.assertReceivingDifferentRows(proxy, key, 0);
    }

    @Test
    public void testUpdateRow() {
        final Map<String, ByteBuffer> map = Maps.newHashMap();
        map.put(TestConstants.COLUMN1, ITUtils.encodeValue(INDEX_COL_VALUE));
        map.put(TestConstants.COLUMN2, ITUtils.encodeValue(6));

        final Row row = getRow(map);
        proxy.insertRow(row.serialize());
        proxy.flush();

        final QueryKey key = ITUtils.createKey(INDEX_COL_VALUE, QueryType.EXACT_KEY);
        proxy.startIndexScan(key.serialize());
        final Row r = getDeserialize(proxy.getNextRow());

        map.put(TestConstants.COLUMN1, ITUtils.encodeValue(3));
        final Row newRow = getRow(map, r.getUUID());
        proxy.updateRow(r.serialize(), newRow.serialize());
        proxy.flush();

        final byte[] result = proxy.getRow(Util.UUIDToBytes(r.getUUID()));

        assertThat(getDeserialize(result), equalTo(newRow));
    }

    private Row getDeserialize(byte[] result) {
        return Row.deserialize(result, getTableSchema());
    }

    private Row getRow(Map<String, ByteBuffer> map) {
        return getRow(map, UUID.randomUUID());
    }

    private Row getRow(Map<String, ByteBuffer> map, UUID r) {
        return new Row(map, r, getTableSchema());
    }

    @Test
    public void testUpdateNullRows() {
        HandlerProxy proxy = factory.createHandlerProxy();
        List<ColumnSchema> columns = Lists.newArrayList();
        List<IndexSchema> indices = Lists.newArrayList();
        columns.add(ColumnSchema.builder(TestConstants.COLUMN1, ColumnType.LONG).build());
        TableSchema schema = new TableSchema(columns, indices);

        String tableName = AdapterType.HBASE.getName() + "/t1";

        int iterations = 10;

        proxy.createTable(tableName, schema.serialize(), 0);
        proxy.openTable(tableName);
        Row row = getRow(Maps.<String, ByteBuffer>newHashMap(), TestConstants.ZERO_UUID);

        List<Row> rows = new ArrayList<Row>();

        for (int j = 0; j < 50; j++) {
            for (int i = 0; i < iterations; i++) {
                proxy.insertRow(row.serialize());
            }

            proxy.flush();

            proxy.startTableScan();
            for (int i = 0; i < iterations; i++) {
                Row deserialized = getDeserialize(proxy.getNextRow());
                deserialized.updateColumn(TestConstants.COLUMN1, ITUtils.encodeValue(0));
                rows.add(deserialized);
            }
            proxy.endScan();

            for (Row r : rows) {
                proxy.updateRow(r.serialize(), r.serialize());
            }

            proxy.flush();
            rows.clear();

            proxy.startTableScan();

            for (int i = 0; i < iterations; i++) {
                byte[] bytes = proxy.getNextRow();
                assertNotNull(bytes);
                assertThat(getDeserialize(bytes).getRecords().get(TestConstants.COLUMN1), equalTo(ITUtils.encodeValue(0)));
            }

            assertNull(proxy.getNextRow());

            proxy.endScan();
            proxy.truncateTable();
        }

        proxy.closeTable();
        proxy.dropTable(tableName);
    }

    private class InsertRows implements ITUtils.ProxyRunnable {
        private long count;

        public InsertRows(long count) {
            this.count = count;
        }

        @Override
        public void run(HandlerProxy proxy) {
            byte[] row;
            for (int i = 0; i < count; i++) {
                row = getRow(fields).serialize();
                proxy.insertRow(row);
            }
            proxy.flush();
        }
    }
}
