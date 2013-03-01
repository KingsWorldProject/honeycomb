package com.nearinfinity.honeycomb.hbase;

import com.nearinfinity.honeycomb.ColumnNotFoundException;
import com.nearinfinity.honeycomb.RowNotFoundException;
import com.nearinfinity.honeycomb.Scanner;
import com.nearinfinity.honeycomb.Table;
import com.nearinfinity.honeycomb.mysql.Row;
import com.nearinfinity.honeycomb.mysql.gen.ColumnMetadata;
import com.nearinfinity.honeycomb.mysql.gen.TableMetadata;
import org.apache.hadoop.hbase.client.HTableInterface;

import java.io.IOException;
import java.util.UUID;

public class HBaseTable implements Table {
    final private HTableInterface hTable;
    final private String name;

    public HBaseTable(HTableInterface hTable, String name) {
        this.hTable = hTable;
        this.name = name;
    }

    @Override
    public void insert(Row row) {

    }

    @Override
    public void update(Row row) throws IOException, RowNotFoundException {
    }

    @Override
    public void delete(UUID uuid) throws IOException, RowNotFoundException {
    }

    @Override
    public void flush() throws IOException {
    }

    @Override
    public Row get(UUID uuid) {
        return null;
    }

    @Override
    public Scanner tableScan() {
        return null;
    }

    @Override
    public Scanner AscIndexScanAt() {
        return null;
    }

    @Override
    public Scanner AscIndexScanAfter() {
        return null;
    }

    @Override
    public Scanner DescIndexScanAt() {
        return null;
    }

    @Override
    public Scanner DescIndexScanAfter() {
        return null;
    }

    @Override
    public Scanner indexScanExact() {
        return null;
    }

    @Override
    public long getAutoIncValue(String column)
            throws IOException, ColumnNotFoundException {
        return 0;
    }

    @Override
    public void setAutoIncValue(String column, long value)
            throws IOException, ColumnNotFoundException {
    }

    @Override
    public ColumnMetadata getColumnMetadata(String column)
            throws IOException, ColumnNotFoundException {
        return null;
    }

    @Override
    public String getTableName() throws IOException {
        return null;
    }

    @Override
    public String getDatabaseName() throws IOException {
        return null;
    }

    @Override
    public void alterTable(TableMetadata tableMetadata) throws IOException {
    }

    @Override
    public void close() throws IOException {
    }
}
