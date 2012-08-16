#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

#include "sql_priv.h"
#include "sql_class.h"           // MYSQL_HANDLERTON_INTERFACE_VERSION
#include "CloudHandler.h"
#include "probes_mysql.h"
#include "sql_plugin.h"
#include "ha_cloud.h"
#include "JVMThreadAttach.h"
#include "mysql_time.h"

#include <sys/time.h>
/*
  If frm_error() is called in table.cc this is called to find out what file
  extensions exist for this handler.

  // TODO: Do any extensions exist for this handler? Doesn't seem like it. - ABC
*/
const char **CloudHandler::bas_ext() const
{
  static const char *cloud_exts[] =
  {
	  NullS
  };

  return cloud_exts;
}

record_buffer *CloudHandler::create_record_buffer(unsigned int length)
{
  DBUG_ENTER("CloudHandler::create_record_buffer");
  record_buffer *r;
  if (!(r = (record_buffer*) my_malloc(sizeof(record_buffer), MYF(MY_WME))))
  {
    DBUG_RETURN(NULL);
  }

  r->length= (int)length;

  if (!(r->buffer= (uchar*) my_malloc(r->length, MYF(MY_WME))))
  {
    my_free(r);
    DBUG_RETURN(NULL);
  }

  DBUG_RETURN(r);
}

void CloudHandler::destroy_record_buffer(record_buffer *r)
{
  DBUG_ENTER("CloudHandler::destroy_record_buffer");
  my_free(r->buffer);
  my_free(r);
  DBUG_VOID_RETURN;
}

double timing(clock_t begin, clock_t end)
{
  return (double)((end - begin)*1000) / CLOCKS_PER_SEC;
}

int CloudHandler::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("CloudHandler::open");

  if (!(share = get_share(name, table)))
  {
    DBUG_RETURN(1);
  }

  thr_lock_data_init(&share->lock, &lock, (void*) this);

  DBUG_RETURN(0);
}

int CloudHandler::close(void)
{
  DBUG_ENTER("CloudHandler::close");

  destroy_record_buffer(rec_buffer);

  DBUG_RETURN(free_share(share));
}

int CloudHandler::write_row(uchar *buf)
{
  DBUG_ENTER("CloudHandler::write_row");

  if (share->crashed)
    DBUG_RETURN(HA_ERR_CRASHED_ON_USAGE);

  ha_statistic_increment(&SSV::ha_write_count);

  int ret = write_row_helper(buf);

  DBUG_RETURN(ret);
}

/*
  This will be called in a table scan right before the previous ::rnd_next()
  call.
*/
int CloudHandler::update_row(const uchar *old_data, uchar *new_data)
{
  DBUG_ENTER("CloudHandler::update_row");

  ha_statistic_increment(&SSV::ha_update_count);

  // TODO: The next two lines should really be some kind of transaction.
  delete_row_helper();
  write_row_helper(new_data);

  DBUG_RETURN(0);
}

int CloudHandler::delete_row(const uchar *buf)
{
  DBUG_ENTER("CloudHandler::delete_row");
  ha_statistic_increment(&SSV::ha_delete_count);
  //stats.records--;
  //share->rows_recorded--;
  delete_row_helper();
  DBUG_RETURN(0);
}

bool CloudHandler::start_bulk_delete()
{
	DBUG_ENTER("CloudHandler::start_bulk_delete");

	JavaVMAttachArgs attachArgs;
	attachArgs.version = JNI_VERSION_1_6;
	attachArgs.name = NULL;
	attachArgs.group = NULL;
	this->jvm->AttachCurrentThread((void**)&this->env, &attachArgs);

	DBUG_RETURN(true);
}

int CloudHandler::end_bulk_delete()
{
	DBUG_ENTER("CloudHandler::end_bulk_delete");

	this->jvm->DetachCurrentThread();

	DBUG_RETURN(0);
}

int CloudHandler::delete_all_rows()
{
	DBUG_ENTER("CloudHandler::delete_all_rows");

	JavaVMAttachArgs attachArgs;
	attachArgs.version = JNI_VERSION_1_6;
	attachArgs.name = NULL;
	attachArgs.group = NULL;
	this->jvm->AttachCurrentThread((void**)&this->env, &attachArgs);

	jstring tableName = string_to_java_string(this->table->alias);
	jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
	jmethodID delete_rows_method = this->env->GetStaticMethodID(adapter_class, "deleteAllRows", "(Ljava/lang/String;)I");

	jboolean result = this->env->CallStaticIntMethod(adapter_class, delete_rows_method, tableName);

	this->jvm->DetachCurrentThread();

	DBUG_RETURN(0);
}

int CloudHandler::truncate()
{
	DBUG_ENTER("CloudHandler::truncate");

	DBUG_RETURN(delete_all_rows());
}

void CloudHandler::drop_table(const char *name)
{
	close();

	delete_table(name);
}

int CloudHandler::delete_table(const char *name)
{
	DBUG_ENTER("CloudHandler::delete_table");

	JavaVMAttachArgs attachArgs;
	attachArgs.version = JNI_VERSION_1_6;
	attachArgs.name = NULL;
	attachArgs.group = NULL;
	this->jvm->AttachCurrentThread((void**)&this->env, &attachArgs);

	const char *alias = extract_table_name_from_path(name);

	jstring tableName = string_to_java_string(alias);
	jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
	jmethodID drop_table_method = this->env->GetStaticMethodID(adapter_class, "dropTable", "(Ljava/lang/String;)Z");

	jboolean result = this->env->CallStaticBooleanMethod(adapter_class, drop_table_method, tableName);

	this->jvm->DetachCurrentThread();

	DBUG_RETURN(0);
}

int CloudHandler::delete_row_helper()
{
  DBUG_ENTER("CloudHandler::delete_row_helper");

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID delete_row_method = this->env->GetStaticMethodID(adapter_class, "deleteRow", "(J)Z");
  jlong java_scan_id = curr_scan_id;

  jboolean result = this->env->CallStaticBooleanMethod(adapter_class, delete_row_method, java_scan_id);

  DBUG_RETURN(0);
}

int CloudHandler::rnd_init(bool scan)
{
  DBUG_ENTER("CloudHandler::rnd_init");

  const char* table_name = this->table->alias;

  JavaVMAttachArgs attachArgs;
  attachArgs.version = JNI_VERSION_1_6;
  attachArgs.name = NULL;
  attachArgs.group = NULL;
  this->jvm->AttachCurrentThread((void**)&this->env, &attachArgs);

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID start_scan_method = this->env->GetStaticMethodID(adapter_class, "startScan", "(Ljava/lang/String;Z)J");
  jstring java_table_name = this->string_to_java_string(table_name);

  jboolean java_scan_boolean = scan ? JNI_TRUE : JNI_FALSE;

  this->curr_scan_id = this->env->CallStaticLongMethod(adapter_class, start_scan_method, java_table_name, java_scan_boolean);

  this->performing_scan = scan;

  DBUG_RETURN(0);
}

int CloudHandler::rnd_next(uchar *buf)
{
  int rc = 0;
  my_bitmap_map *orig_bitmap;

  ha_statistic_increment(&SSV::ha_read_rnd_next_count);
  DBUG_ENTER("CloudHandler::rnd_next");

  orig_bitmap= dbug_tmp_use_all_columns(table, table->write_set);

  MYSQL_READ_ROW_START(table_share->db.str, table_share->table_name.str, TRUE);

  memset(buf, 0, table->s->null_bytes);

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID next_row_method = this->env->GetStaticMethodID(adapter_class, "nextRow", "(J)Lcom/nearinfinity/mysqlengine/jni/Row;");
  jlong java_scan_id = curr_scan_id;
  jobject row = this->env->CallStaticObjectMethod(adapter_class, next_row_method, java_scan_id);

  jclass row_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/Row");
  jmethodID get_keys_method = this->env->GetMethodID(row_class, "getKeys", "()[Ljava/lang/String;");
  jmethodID get_vals_method = this->env->GetMethodID(row_class, "getValues", "()[[B");
  jmethodID get_row_map_method = this->env->GetMethodID(row_class, "getRowMap", "()Ljava/util/Map;");
  jmethodID get_uuid_method = this->env->GetMethodID(row_class, "getUUID", "()[B");

  jobject row_map = this->env->CallObjectMethod(row, get_row_map_method);

  if (row_map == NULL)
  {
    dbug_tmp_restore_column_map(table->write_set, orig_bitmap);
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  jarray keys = (jarray) this->env->CallObjectMethod(row, get_keys_method);
  jarray vals = (jarray) this->env->CallObjectMethod(row, get_vals_method);
  jbyteArray uuid = (jbyteArray) this->env->CallObjectMethod(row, get_uuid_method);

  this->ref = (uchar*) this->env->GetByteArrayElements(uuid, JNI_FALSE);
  this->ref_length = 16;

  java_to_sql(buf, row_map);

  dbug_tmp_restore_column_map(table->write_set, orig_bitmap);

  stats.records++;
  MYSQL_READ_ROW_DONE(rc);

  DBUG_RETURN(rc);
}

void CloudHandler::java_to_sql(uchar* buf, jobject row_map)
{
  jboolean is_copy = JNI_FALSE;

  for (int i = 0; i < table->s->fields; i++)
  {
    Field *field = table->field[i];
    const char* key = field->field_name;
    jstring java_key = string_to_java_string(key);
    jbyteArray java_val = java_map_get(row_map, java_key);
    if (java_val == NULL) {
      field->set_null();
      continue;
    }
    char* val = (char*) this->env->GetByteArrayElements(java_val, &is_copy);
    jsize val_length = this->env->GetArrayLength(java_val);

    my_ptrdiff_t offset = (my_ptrdiff_t) (buf - this->table->record[0]);
    enum_field_types field_type = field->type();
    field->move_field_offset(offset);

    if (field_type == MYSQL_TYPE_LONG ||
        field_type == MYSQL_TYPE_SHORT ||
        field_type == MYSQL_TYPE_LONGLONG ||
        field_type == MYSQL_TYPE_INT24 ||
        field_type == MYSQL_TYPE_TINY ||
        field_type == MYSQL_TYPE_YEAR)
    {
      longlong long_value = *(longlong*)val;
      if(this->is_little_endian())
      {
        long_value = __builtin_bswap64(long_value);
      }

      field->store(long_value, false);
    }
    else if (field_type == MYSQL_TYPE_FLOAT ||
             field_type == MYSQL_TYPE_DECIMAL ||
             field_type == MYSQL_TYPE_NEWDECIMAL ||
             field_type == MYSQL_TYPE_DOUBLE)
    {
      double double_value;
      if (this->is_little_endian())
      {
        longlong* long_ptr = (longlong*)val;
        longlong swapped_long = __builtin_bswap64(*long_ptr);
        double_value = *(double*)&swapped_long;
      }
      else
      {
        double_value = *(double*)val;
      }

      field->store(double_value);
    }
    else if (field_type == MYSQL_TYPE_VARCHAR
        || field_type == MYSQL_TYPE_STRING
        || field_type == MYSQL_TYPE_VAR_STRING
        || field_type == MYSQL_TYPE_BLOB
        || field_type == MYSQL_TYPE_TINY_BLOB
        || field_type == MYSQL_TYPE_MEDIUM_BLOB
        || field_type == MYSQL_TYPE_LONG_BLOB
        || field_type == MYSQL_TYPE_ENUM)
    {
      field->store(val, val_length, &my_charset_bin);
    }
    else if (field_type == MYSQL_TYPE_TIME
        || field_type == MYSQL_TYPE_DATE
        || field_type == MYSQL_TYPE_DATETIME
        || field_type == MYSQL_TYPE_TIMESTAMP
        || field_type == MYSQL_TYPE_NEWDATE)
    {
      MYSQL_TIME mysql_time;

      int was_cut;
      int warning;

      switch (field_type)
      {
      case MYSQL_TYPE_TIME:
        str_to_time(val, field->field_length, &mysql_time, &warning);
        break;
      default:
        str_to_datetime(val, field->field_length, &mysql_time, TIME_FUZZY_DATE, &was_cut);
        break;
      }

      field->store_time(&mysql_time, mysql_time.time_type);
    }

    field->move_field_offset(-offset);
    this->env->ReleaseByteArrayElements(java_val, (jbyte*)val, 0);
  }
  return;
}

int CloudHandler::external_lock(THD *thd, int lock_type)
{
  DBUG_ENTER("CloudHandler::external_lock");
  DBUG_RETURN(0);
}

void CloudHandler::position(const uchar *record)
{
  DBUG_ENTER("CloudHandler::position");
  DBUG_VOID_RETURN;
}

int CloudHandler::rnd_pos(uchar *buf, uchar *pos)
{
  int rc = 0;
  DBUG_ENTER("CloudHandler::rnd_pos");
  ha_statistic_increment(&SSV::ha_read_rnd_count); // Boilerplate
  MYSQL_READ_ROW_START(table_share->db.str, table_share->table_name.str, FALSE);

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID get_row_method = this->env->GetStaticMethodID(adapter_class, "getRow", "(JLjava/lang/String;[B)Lcom/nearinfinity/mysqlengine/jni/Row;");
  jlong java_scan_id = curr_scan_id;
  jobject row = this->env->CallStaticObjectMethod(adapter_class, get_row_method, java_scan_id, pos);

  jclass row_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/Row");
  jmethodID get_keys_method = this->env->GetMethodID(row_class, "getKeys", "()[Ljava/lang/String;");
  jmethodID get_vals_method = this->env->GetMethodID(row_class, "getValues", "()[[B");

  jarray keys = (jarray) this->env->CallObjectMethod(row, get_keys_method);
  jarray vals = (jarray) this->env->CallObjectMethod(row, get_vals_method);

  jboolean is_copy = JNI_FALSE;

  if (this->env->GetArrayLength(keys) == 0 ||
      this->env->GetArrayLength(vals) == 0)
  {
    DBUG_RETURN(HA_ERR_WRONG_COMMAND);
  }

  //store_field_values(buf, keys, vals);

  MYSQL_READ_ROW_DONE(rc);
  DBUG_RETURN(rc);
}

int CloudHandler::rnd_end()
{
  DBUG_ENTER("CloudHandler::rnd_end");

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID end_scan_method = this->env->GetStaticMethodID(adapter_class, "endScan", "(J)V");
  jlong java_scan_id = curr_scan_id;

  this->env->CallStaticVoidMethod(adapter_class, end_scan_method, java_scan_id);
  this->jvm->DetachCurrentThread();

  curr_scan_id = -1;
  this->performing_scan = false;
  DBUG_RETURN(0);
}

void CloudHandler::start_bulk_insert(ha_rows rows)
{
  DBUG_ENTER("CloudHandler::start_bulk_insert");
  JavaVMAttachArgs attachArgs;
  attachArgs.version = JNI_VERSION_1_6;
  attachArgs.name = NULL;
  attachArgs.group = NULL;
  this->jvm->AttachCurrentThread((void**)&this->env, &attachArgs);

  DBUG_VOID_RETURN;
}

int CloudHandler::end_bulk_insert()
{
  DBUG_ENTER("CloudHandler::end_bulk_insert");

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID end_write_method = this->env->GetStaticMethodID(adapter_class, "flushWrites", "()V");
  this->env->CallStaticVoidMethod(adapter_class, end_write_method);

  this->jvm->DetachCurrentThread();
  DBUG_RETURN(0);
}

int CloudHandler::create(const char *name, TABLE *table_arg,
                         HA_CREATE_INFO *create_info)
{
  DBUG_ENTER("CloudHandler::create");

//  JVMThreadAttach attached_thread(&this->env, this->jvm);

	JavaVMAttachArgs attachArgs;
	attachArgs.version = JNI_VERSION_1_6;
	attachArgs.name = NULL;
	attachArgs.group = NULL;
	this->jvm->AttachCurrentThread((void**)&this->env, &attachArgs);

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  if (adapter_class == NULL)
  {
    this->print_java_exception(this->env);
    ERROR(("Could not find adapter class HBaseAdapter"));
    DBUG_RETURN(1);
  }

  const char* table_name = create_info->alias;

  jobject columnMap = this->create_java_map();

  for (Field **field = table_arg->field ; *field ; field++)
  {
	  jobject metadataList = this->get_field_metadata(*field, table_arg);
	  this->java_map_insert(columnMap, string_to_java_string((*field)->field_name), metadataList);
  }

  jmethodID create_table_method = this->env->GetStaticMethodID(adapter_class, "createTable", "(Ljava/lang/String;Ljava/util/Map;)Z");
  jboolean result = this->env->CallStaticBooleanMethod(adapter_class, create_table_method, string_to_java_string(table_name), columnMap);
  INFO(("Result of createTable: %d", result));
  this->print_java_exception(this->env);

  this->jvm->DetachCurrentThread();

  DBUG_RETURN(0);
}

jobject CloudHandler::create_java_list()
{
	jclass list_class = this->env->FindClass("java/util/LinkedList");
	jmethodID list_constructor = this->env->GetMethodID(list_class, "<init>", "()V");
	return this->env->NewObject(list_class, list_constructor);
}

jobject CloudHandler::create_metadata_enum_object(const char *name)
{
  jclass metadata_class = this->env->FindClass("com/nearinfinity/mysqlengine/ColumnMetadata");
  jfieldID enum_field = this->env->GetStaticFieldID(metadata_class, name, "Lcom/nearinfinity/mysqlengine/ColumnMetadata;");
  jobject enum_object = this->env->GetStaticObjectField(metadata_class, enum_field);

  return enum_object;
}

jobject CloudHandler::get_field_metadata(Field *field, TABLE *table_arg)
{
	jobject list = this->create_java_list();
	hbase_data_type essentialType = this->extract_field_type(field);

	switch (essentialType)
	{
	case JAVA_STRING:
	  this->java_list_add(list, create_metadata_enum_object("STRING"));
		break;
	case JAVA_TIME:
	  this->java_list_add(list, create_metadata_enum_object("TIME"));
		break;
	case JAVA_DOUBLE:
	  this->java_list_add(list, create_metadata_enum_object("DOUBLE"));
		break;
	case JAVA_LONG:
	  this->java_list_add(list, create_metadata_enum_object("LONG"));
		break;
	default:
		break;
	}

	if (field->real_maybe_null())
	{
	  this->java_list_add(list, create_metadata_enum_object("IS_NULLABLE"));
	}

	// 64 is obviously some key flag indicating no primary key, but I have no idea where it's defined. Will fix later. - ABC
	if (table_arg->s->primary_key != 64)
	{
	  if (strcmp(table_arg->s->key_info[table_arg->s->primary_key].key_part->field->field_name, field->field_name) == 0)
	    {
	      this->java_list_add(list, create_metadata_enum_object("PRIMARY_KEY"));
	    }
	}

	return list;

}

void CloudHandler::java_list_add(jobject list, jobject obj)
{
	jclass list_class = this->env->FindClass("java/util/LinkedList");
	jmethodID add_method = this->env->GetMethodID(list_class, "add", "(Ljava/lang/Object;)Z");

	this->env->CallBooleanMethod(list, add_method, obj);
}

hbase_data_type CloudHandler::extract_field_type(Field *field)
{
	int fieldType = field->type();
	hbase_data_type essentialType;

	if (fieldType == MYSQL_TYPE_LONG
	        || fieldType == MYSQL_TYPE_SHORT
	        || fieldType == MYSQL_TYPE_TINY
	        || fieldType == MYSQL_TYPE_LONGLONG
	        || fieldType == MYSQL_TYPE_INT24
	        || fieldType == MYSQL_TYPE_ENUM
	        || fieldType == MYSQL_TYPE_YEAR)
	{
		essentialType = JAVA_LONG;
	}
	else if (fieldType == MYSQL_TYPE_DOUBLE
             || fieldType == MYSQL_TYPE_FLOAT
             || fieldType == MYSQL_TYPE_DECIMAL
             || fieldType == MYSQL_TYPE_NEWDECIMAL)
	{
		essentialType = JAVA_DOUBLE;
	}
	else if (fieldType == MYSQL_TYPE_TIME
			|| fieldType == MYSQL_TYPE_DATE
			|| fieldType == MYSQL_TYPE_NEWDATE
			|| fieldType == MYSQL_TYPE_DATETIME
			|| fieldType == MYSQL_TYPE_TIMESTAMP)
	{
		essentialType = JAVA_TIME;
	}
	else if (fieldType == MYSQL_TYPE_VARCHAR
            || fieldType == MYSQL_TYPE_STRING
            || fieldType == MYSQL_TYPE_VAR_STRING
            || fieldType == MYSQL_TYPE_BLOB
            || fieldType == MYSQL_TYPE_TINY_BLOB
            || fieldType == MYSQL_TYPE_MEDIUM_BLOB
            || fieldType == MYSQL_TYPE_LONG_BLOB)
	{
		essentialType = JAVA_STRING;
	}
	else
	{
		essentialType = UNKNOWN_TYPE;
	}

	return essentialType;
}

THR_LOCK_DATA **CloudHandler::store_lock(THD *thd, THR_LOCK_DATA **to, enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type=lock_type;
  *to++= &lock;
  return to;
}

/*
  Free lock controls.
*/
int CloudHandler::free_share(CloudShare *share)
{
  DBUG_ENTER("CloudHandler::free_share");
  mysql_mutex_lock(cloud_mutex);
  int result_code= 0;
  if (!--share->use_count)
  {
    my_hash_delete(cloud_open_tables, (uchar*) share);
    thr_lock_delete(&share->lock);
    //mysql_mutex_destroy(&share->mutex);
    my_free(share);
  }
  mysql_mutex_unlock(cloud_mutex);

  DBUG_RETURN(result_code);
}

int CloudHandler::info(uint)
{
  DBUG_ENTER("CloudHandler::info");
  if (stats.records < 2)
    stats.records= 2;
  DBUG_RETURN(0);
}

CloudShare *CloudHandler::get_share(const char *table_name, TABLE *table)
{
  CloudShare *share;
  char *tmp_path_name;
  uint path_length;

  rec_buffer= create_record_buffer(table->s->reclength);

  if (!rec_buffer)
  {
    DBUG_PRINT("CloudHandler", ("Ran out of memory while allocating record buffer"));

    return NULL;
  }

  mysql_mutex_lock(cloud_mutex);
  path_length=(uint) strlen(table_name);

  /*
  If share is not present in the hash, create a new share and
  initialize its members.
  */
  if (!(share=(CloudShare*) my_hash_search(cloud_open_tables,
              (uchar*) table_name,
              path_length)))
  {
    if (!my_multi_malloc(MYF(MY_WME | MY_ZEROFILL),
                         &share, sizeof(*share),
                         &tmp_path_name, path_length+1,
                         NullS))
    {
      mysql_mutex_unlock(cloud_mutex);
      return NULL;
    }
  }

  share->use_count= 0;
  share->table_path_length= path_length;
  share->path_to_table= tmp_path_name;
  share->crashed= FALSE;
  share->rows_recorded= 0;

  if (my_hash_insert(cloud_open_tables, (uchar*) share))
    goto error;
  thr_lock_init(&share->lock);

  share->use_count++;
  mysql_mutex_unlock(cloud_mutex);

  return share;

error:
  mysql_mutex_unlock(cloud_mutex);
  my_free(share);

  return NULL;
}

int CloudHandler::extra(enum ha_extra_function operation)
{
  DBUG_ENTER("CloudHandler::extra");
  DBUG_RETURN(0);
}

uint32 CloudHandler::max_row_length()
{
  uint32 length = (uint32)(table->s->reclength + table->s->fields*2);

  uint *ptr, *end;
  for (ptr = table->s->blob_field, end=ptr + table->s->blob_fields ; ptr != end ; ptr++)
  {
    if (!table->field[*ptr]->is_null())
      length += 2 + ((Field_blob*)table->field[*ptr])->get_length();
  }

  return length;
}

/* Set up the JNI Environment, and then persist the row to HBase.
 * This helper calls sql_to_java, which returns the row information
 * as a jobject to be sent to the HBaseAdapter.
 */
int CloudHandler::write_row_helper(uchar* buf) {
  DBUG_ENTER("CloudHandler::write_row_helper");

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID write_row_method = this->env->GetStaticMethodID(adapter_class, "writeRow", "(Ljava/lang/String;Ljava/util/Map;[B)Z");
  jstring java_table_name = this->string_to_java_string(this->table->alias);
  jobject java_row_map = sql_to_java();
  uint32 row_length = this->max_row_length();
  jbyteArray uniReg = this->env->NewByteArray(row_length);
  uchar* buffer = new uchar[row_length];
  memcpy(buffer, buf, table->s->null_bytes);
  uchar* ptr = buffer + table->s->null_bytes;
  for (Field **field_ptr = table->field ; *field_ptr ; field_ptr++)
  {
    Field* field = *field_ptr;
    if (!field->is_null())
    {
      ptr = field->pack(ptr, buf + field->offset(buf));
    }
  }

  this->env->SetByteArrayRegion(uniReg, 0, row_length, (jbyte*)buffer);
  delete buffer;

  this->env->CallStaticBooleanMethod(adapter_class, write_row_method, java_table_name, java_row_map, uniReg);

  DBUG_RETURN(0);
}

/* Read fields into a java map.
 */
jobject CloudHandler::sql_to_java() {
  jobject java_map = this->create_java_map();
  // Boilerplate stuff every engine has to do on writes

  if (table->timestamp_field_type & TIMESTAMP_AUTO_SET_ON_INSERT)
    table->timestamp_field->set_time();

  my_bitmap_map *old_map = dbug_tmp_use_all_columns(table, table->read_set);

  char attribute_buffer[1024];
  String attribute(attribute_buffer, sizeof(attribute_buffer), &my_charset_bin);

  for (Field **field_ptr=table->field; *field_ptr; field_ptr++)
  {
    Field * field = *field_ptr;
    jstring field_name = this->string_to_java_string(field->field_name);

    memset(rec_buffer->buffer, 0, rec_buffer->length);

    const bool is_null = field->is_null();

    if (is_null )
    {
      java_map_insert(java_map, field_name, NULL);
      continue;
    }

    hbase_data_type fieldType = this->extract_field_type(field);
    uint actualFieldSize = field->field_length;

    if (fieldType == JAVA_LONG)
    {
      longlong field_value = field->val_int();
      if(this->is_little_endian())
      {
        field_value = __builtin_bswap64(field_value);
      }

      actualFieldSize = sizeof(longlong);
      memcpy(rec_buffer->buffer, &field_value, sizeof(longlong));
    }
    else if (fieldType == JAVA_DOUBLE)
    {
      double field_value = field->val_real();
      actualFieldSize = sizeof(double);
      if(this->is_little_endian())
      {
        longlong* long_value = (longlong*)&field_value;
        *long_value = __builtin_bswap64(*long_value);
      }
      memcpy(rec_buffer->buffer, &field_value, sizeof(longlong));
    }
	else if (fieldType == JAVA_TIME)
	{
		MYSQL_TIME mysql_time;
		field->get_time(&mysql_time);

		switch (field->type())
		{
			case MYSQL_TYPE_DATE:
			case MYSQL_TYPE_NEWDATE:
				mysql_time.time_type = MYSQL_TIMESTAMP_DATE;
				break;
			case MYSQL_TYPE_DATETIME:
			case MYSQL_TYPE_TIMESTAMP:
				mysql_time.time_type = MYSQL_TIMESTAMP_DATETIME;
				break;
			case MYSQL_TYPE_TIME:
				mysql_time.time_type = MYSQL_TIMESTAMP_TIME;
				break;
			default:
				mysql_time.time_type = MYSQL_TIMESTAMP_NONE;
				break;
		}

		char timeString[MAX_DATE_STRING_REP_LENGTH];
		my_TIME_to_str(&mysql_time, timeString);

		actualFieldSize = strlen(timeString);
		memcpy(rec_buffer->buffer, timeString, actualFieldSize);
	}
    else if (fieldType == JAVA_STRING)
    {
      field->val_str(&attribute);
      actualFieldSize = attribute.length();
      memcpy(rec_buffer->buffer, attribute.ptr(), attribute.length());
    }
	else
	{
		memcpy(rec_buffer->buffer, field->ptr, field->field_length);
	}

    jbyteArray java_bytes = this->convert_value_to_java_bytes(rec_buffer->buffer, actualFieldSize);

    java_map_insert(java_map, field_name, java_bytes);
  }

  dbug_tmp_restore_column_map(table->read_set, old_map);

  return java_map;
}

const char* CloudHandler::java_to_string(jstring j_str)
{
  return this->env->GetStringUTFChars(j_str, NULL);
}

jstring CloudHandler::string_to_java_string(const char* string)
{
  return this->env->NewStringUTF(string);
}

jobject CloudHandler::create_java_map()
{
  jclass map_class = this->env->FindClass("java/util/TreeMap");
  jmethodID constructor = this->env->GetMethodID(map_class, "<init>", "()V");
  return this->env->NewObject(map_class, constructor);
}

jobject CloudHandler::java_map_insert(jobject java_map, jobject key, jobject value)
//jobject CloudHandler::java_map_insert(jobject java_map, jstring key, jbyteArray value)
{
  jclass map_class = this->env->FindClass("java/util/TreeMap");
  jmethodID put_method = this->env->GetMethodID(map_class, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

  return this->env->CallObjectMethod(java_map, put_method, key, value);
}

jbyteArray CloudHandler::java_map_get(jobject java_map, jstring key)
{
  jclass map_class = this->env->FindClass("java/util/TreeMap");
  jmethodID get_method = this->env->GetMethodID(map_class, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");

  return (jbyteArray) this->env->CallObjectMethod(java_map, get_method, key);
}

jboolean CloudHandler::java_map_is_empty(jobject java_map)
{
  jclass map_class = this->env->FindClass("java/util/TreeMap");
  jmethodID is_empty_method = this->env->GetMethodID(map_class, "isEmpty", "()Z");
  jboolean result = env->CallBooleanMethod(java_map, is_empty_method);
  return (bool) result;
}

jbyteArray CloudHandler::convert_value_to_java_bytes(uchar* value, uint32 length)
{
  jbyteArray byteArray = this->env->NewByteArray(length);
  jbyte *java_bytes = this->env->GetByteArrayElements(byteArray, 0);

  memcpy(java_bytes, value, length);

  this->env->SetByteArrayRegion(byteArray, 0, length, java_bytes);
  this->env->ReleaseByteArrayElements(byteArray, java_bytes, 0);

  return byteArray;
}

/*ha_rows CloudHandler::records_in_range(uint index, key_range* min_key, key_range* max_key)
{
  DBUG_ENTER("CloudHandler::records_in_range");

  KEY* key = this->table->key_info + index;
  const uchar* min_key_value = min_key->key;
  const uchar* max_key_value = max_key->key;

  DBUG_RETURN(0);
}*/

int CloudHandler::index_init(uint idx, bool sorted)
{
  DBUG_ENTER("CloudHandler::index_init");
  
  this->active_index = idx;
  
  const char* table_name = this->table->alias;
  const char* column_name = this->table->s->key_info[idx].key_part->field->field_name;
  for (Field **field_ptr=table->field; *field_ptr; field_ptr++)
  {
    Field * field = *field_ptr;
    if (strcmp(field->field_name, column_name) == 0)
    {
      this->index_field_type = field->type();
      break;
    }
  }

  JavaVMAttachArgs attachArgs;
  attachArgs.version = JNI_VERSION_1_6;
  attachArgs.name = NULL;
  attachArgs.group = NULL;
  this->jvm->AttachCurrentThread((void**)&this->env, &attachArgs);

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID start_scan_method = this->env->GetStaticMethodID(adapter_class, "startIndexScan", "(Ljava/lang/String;Ljava/lang/String;)J");
  jstring java_table_name = this->string_to_java_string(table_name);
  jstring java_column_name = this->string_to_java_string(column_name);

  this->curr_scan_id = this->env->CallStaticLongMethod(adapter_class, start_scan_method, java_table_name, java_column_name);
  
  DBUG_RETURN(0);
}

int CloudHandler::index_end()
{
  DBUG_ENTER("CloudHandler::index_end");
  
  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID end_scan_method = this->env->GetStaticMethodID(adapter_class, "endScan", "(J)V");
  jlong java_scan_id = curr_scan_id;

  this->env->CallStaticVoidMethod(adapter_class, end_scan_method, java_scan_id);
  this->jvm->DetachCurrentThread();

  this->curr_scan_id = -1;
  this->active_index = -1;

  DBUG_RETURN(0);
}

int CloudHandler::index_read(uchar *buf, const uchar *key, uint key_len, enum ha_rkey_function find_flag)
{
  DBUG_ENTER("CloudHandler::index_read");
  
  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID index_read_method = this->env->GetStaticMethodID(adapter_class, "indexRead", "(J[BLcom/nearinfinity/mysqlengine/jni/IndexReadType;)[B");
  jlong java_scan_id = this->curr_scan_id;
  uchar* key_copy;
  if (this->is_integral_field(this->index_field_type))
  {
    key_copy = new uchar[sizeof(longlong)];
    if (key_len == sizeof(int))
    {
      int* int_key = (int*)key;
      longlong long_key = (longlong)*int_key;
      memcpy(key_copy, &long_key, sizeof(longlong));
      key_len = sizeof(longlong);
    }
    else
    {
      longlong* long_key = (longlong*)key;
      memcpy(key_copy, long_key, sizeof(longlong));
    }
  }
  else
  {
    key_copy = new uchar[key_len];
    memcpy(key_copy, key, key_len);
  }

  this->make_big_endian(key_copy, key_len);
  jbyteArray java_key = this->env->NewByteArray(key_len);
  this->env->SetByteArrayRegion(java_key, 0, key_len, (jbyte*)key_copy);
  delete key_copy;
  jobject java_find_flag = this->java_find_flag(find_flag);
  jobject result = this->env->CallStaticObjectMethod(adapter_class, index_read_method, java_scan_id, java_key, java_find_flag);
  jbyteArray uniReg = (jbyteArray)result;
  if(uniReg == NULL)
  {
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  this->unpack_index(buf, uniReg);

  DBUG_RETURN(0);
}

int CloudHandler::index_next(uchar *buf)
{
  int rc = 0;
  my_bitmap_map *orig_bitmap;

  DBUG_ENTER("CloudHandler::index_next");

  orig_bitmap= dbug_tmp_use_all_columns(table, table->write_set);

  MYSQL_READ_ROW_START(table_share->db.str, table_share->table_name.str, TRUE);

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID index_next_method = this->env->GetStaticMethodID(adapter_class, "nextIndexRow", "(J)[B");
  jlong java_scan_id = this->curr_scan_id;
  jobject result = this->env->CallStaticObjectMethod(adapter_class, index_next_method, java_scan_id);
  jbyteArray uniReg = (jbyteArray)result;

  if (uniReg == NULL)
  {
    dbug_tmp_restore_column_map(table->write_set, orig_bitmap);
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  this->unpack_index(buf, uniReg);

  dbug_tmp_restore_column_map(table->write_set, orig_bitmap);

  MYSQL_READ_ROW_DONE(rc);

  DBUG_RETURN(rc);
}

jobject CloudHandler::java_find_flag(enum ha_rkey_function find_flag)
{
  const char* index_type_path = "Lcom/nearinfinity/mysqlengine/jni/IndexReadType;";
  jclass read_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/IndexReadType");
  jfieldID field_id;
  if (find_flag == HA_READ_KEY_EXACT)
  {
    field_id = this->env->GetStaticFieldID(read_class, "HA_READ_KEY_EXACT", index_type_path);
  }
  else if(find_flag == HA_READ_AFTER_KEY)
  {
    field_id = this->env->GetStaticFieldID(read_class, "HA_READ_AFTER_KEY", index_type_path);
  }
  else if(find_flag == HA_READ_KEY_OR_NEXT)
  {
    field_id = this->env->GetStaticFieldID(read_class, "HA_READ_KEY_OR_NEXT", index_type_path);
  }
  else if(find_flag == HA_READ_KEY_OR_PREV)
  {
    field_id = this->env->GetStaticFieldID(read_class, "HA_READ_KEY_OR_PREV", index_type_path);
  }
  else if(find_flag == HA_READ_BEFORE_KEY)
  {
    field_id = this->env->GetStaticFieldID(read_class, "HA_READ_BEFORE_KEY", index_type_path);
  }
  else
  {
    return NULL;
  }

  return this->env->GetStaticObjectField(read_class, field_id);
}

void CloudHandler::unpack_index(uchar* buf, jbyteArray uniReg)
{
  jbyte* buffer = this->env->GetByteArrayElements(uniReg, NULL);
  jbyte* ptr = buffer;
  memset(buf, 0, table->s->reclength);
  memcpy(buf, ptr, table->s->null_bytes);
  ptr += table->s->null_bytes;
  for (Field **field_ptr = table->field ; *field_ptr ; field_ptr++)
  {
    Field* field = *field_ptr;
    if (!field->is_null_in_record(buf))
    {
      ptr = (jbyte*)field->unpack(buf + field->offset(table->record[0]), (uchar*)ptr);
    }
  }

  this->env->ReleaseByteArrayElements(uniReg, buffer, 0);
}

int CloudHandler::index_prev(uchar *buf)
{
  int rc = 0;
  my_bitmap_map *orig_bitmap;

  DBUG_ENTER("CloudHandler::index_prev");

  orig_bitmap= dbug_tmp_use_all_columns(table, table->write_set);

  MYSQL_READ_ROW_START(table_share->db.str, table_share->table_name.str, TRUE);

  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID index_next_method = this->env->GetStaticMethodID(adapter_class, "nextIndexRow", "(J)[B");
  jlong java_scan_id = this->curr_scan_id;
  jobject result = this->env->CallStaticObjectMethod(adapter_class, index_next_method, java_scan_id);
  jbyteArray uniReg = (jbyteArray)result;

  if (uniReg == NULL)
  {
    dbug_tmp_restore_column_map(table->write_set, orig_bitmap);
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  this->unpack_index(buf, uniReg);

  dbug_tmp_restore_column_map(table->write_set, orig_bitmap);

  MYSQL_READ_ROW_DONE(rc);

  DBUG_RETURN(rc);
}

int CloudHandler::index_first(uchar *buf)
{
  DBUG_ENTER("CloudHandler::index_first");
  
  jclass adapter_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/HBaseAdapter");
  jmethodID index_read_method = this->env->GetStaticMethodID(adapter_class, "indexRead", "(J[BLcom/nearinfinity/mysqlengine/jni/IndexReadType;)[B");
  jlong java_scan_id = this->curr_scan_id;
  
  jclass read_class = this->env->FindClass("com/nearinfinity/mysqlengine/jni/IndexReadType");
  jfieldID field_id = this->env->GetStaticFieldID(read_class, "INDEX_FIRST", "Lcom/nearinfinity/mysqlengine/jni/IndexReadType;");
  jobject java_find_flag = this->env->GetStaticObjectField(read_class, field_id);
  
  jobject result = this->env->CallStaticObjectMethod(adapter_class, index_read_method, java_scan_id, NULL, java_find_flag);
  jbyteArray uniReg = (jbyteArray)result;
  
  if(uniReg == NULL)
  {
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  this->unpack_index(buf, uniReg);

  DBUG_RETURN(0);
}

int CloudHandler::index_last(uchar *buf)
{
  DBUG_ENTER("CloudHandler::index_last");
  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}