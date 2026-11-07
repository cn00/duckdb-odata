CREATE TABLE IF NOT EXISTS lambda_customers (
    id BIGINT PRIMARY KEY,
    name VARCHAR NOT NULL,
    active BOOLEAN DEFAULT true
);

INSERT INTO lambda_customers VALUES
    (1, 'Lambda demo customer', true)
ON CONFLICT DO NOTHING;

-- set extension_directories = '/opt/duckdb/extensions';
LOAD '/opt/duckdb/extensions/aws.duckdb_extension';
LOAD '/opt/duckdb/extensions/httpfs.duckdb_extension';

-- -- s3 view
CREATE SCHEMA IF NOT EXISTS s3;
-- CREATE SECRET cfg (TYPE S3, PROVIDER credential_chain, CHAIN 'config', PROFILE 'use1');
CREATE SECRET cfg (TYPE S3, PROVIDER credential_chain);
CREATE OR REPLACE VIEW s3.fire AS
    SELECT * FROM read_csv_auto('s3://bucket/file.csv');

CALL odata_expose_schema('s3');

CALL odata_expose('lambda_customers');
CALL odata_entity('lambda_customers', key := 'id');
