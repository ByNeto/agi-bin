<?php
/* $Id: audio_server_api.inc,v 1.6 2006/11/01 12:03:22 leob Exp $ */

/**
 * @file
 * Basic functions that allow users to create and manage audio entries in a remote Drupal server
 *
 */


// -----------------------------------------------------------------------------
// include required files
// -----------------------------------------------------------------------------
foreach (array( 'common.inc', 'error_handler.inc', 'xmlrpc.inc') as $file) {
   require_once($file);
}
// ----------------------------------------------------------------------------
// constants
// -----------------------------------------------------------------------------

  // Status of a audio entry

  define( 'ASA_TO_BE_UPLOADED', 'to be uploaded');
  define( 'ASA_UPLOADED', 'uploaded');
  define( 'ASA_ERROR', 'error');


// -----------------------------------------------------------------------------
// global variables
// -----------------------------------------------------------------------------

global $configuration_file;
if (!isset($configuration_file)) {
  $configuration_file = "audio_server_api.ini";
}
$asa_config = parse_ini_file($configuration_file, TRUE);
$asa_config = $asa_config['audio_server_api'];

 
global $asa_db_options;
if (!isset($asa_db_options)) {
  $asa_db_options['server'] = $asa_config['asa_db_server'];
  $asa_db_options['db_name'] = $asa_config['asa_db_name'];
  $asa_db_options['user'] = $asa_config['asa_db_user'];
  $asa_db_options['password'] = $asa_config['asa_db_password'];
}

  
global $asa_tmp_dir;
if (!isset($asa_tmp_dir)) {
  $asa_tmp_dir = isset($asa_config['asa_tmp_dir'])? $asa_config['asa_tmp_dir'] : '/data/tmp';
}

global $asa_sox;
if (!isset($asa_sox)) {
  $asa_sox = isset($asa_config['asa_sox'])? $asa_config['asa_sox'] : '/usr/bin/sox';
}

global $asa_lame;
if (!isset($asa_lame)) {
  $asa_lame = isset($asa_config['asa_lame'])? $asa_config['asa_lame'] : '/usr/bin/lame';
}

global $asa_mpg123;
if (!isset($asa_mpg123)) {
  $asa_mpg123 = isset($asa_config['asa_mpg123'])? $asa_config['asa_mpg123'] : '/usr/local/bin/mpg123';
}


if (!function_exists('mime_content_type')){
  function mime_content_type($f) {
    return system(trim('file -bi ' . escapeshellarg($f)));
  }
}


// -----------------------------------------------------------------------------
// initialization
// -----------------------------------------------------------------------------


  // time limit for the upload transactions
  // set_time_limit( 60);


// -----------------------------------------------------------------------------
// public function definitions
// -----------------------------------------------------------------------------

/*
 * Add a new audio entry to the queue so that it can be later uploaded to the server.
 * 
 * @param $xmlrpc_server
 *   URL of the xmlrpc.php file of the server where the entry is to be uploaded to
 * 
 * @param $user_info
 *   array with user name, password of the author of the entry
 * 
 * @param $entry_info
 *   array with all the values associated with the new audio entry (title, body, etc.)
 * 
 * @return
 *   boolean TRUE indicates success.  If FALSE, check eh_error_msg() for explanation.
 */
function asa_add_audio_entry_to_upload_queue($xmlrpc_server, $user_info, $entry_info) {

  global $asa_db_options;


  // open connection to database server
  $db_link = mysql_connect($asa_db_options['server'], $asa_db_options['user'], $asa_db_options['password']);

  if (!$db_link) {
    eh_error('Could not connect to database server: ' . mysql_error());
    return FALSE;
  }

  // select database to be used

  $db = mysql_select_db( $asa_db_options['db_name'], $db_link);

  if (!$db_link) {
    eh_error('Can\'t use ' . $asa_db_options['db_name'] . ' : ' . mysql_error());
    return FALSE;
  }

  // insert audio entry in the database

  $upload_status = ASA_TO_BE_UPLOADED;
  $tags = (isset($entry_info['tags']))?$entry_info['tags']:NULL;
  $extra_fields = (isset($entry_info['extra_fields']))?$entry_info['extra_fields']:array();
  $notes = 'empty note';

  $query = "INSERT INTO asa_upload_queue
            ( title, body, file, xmlrpc_url, user_name, password, status, promote, downloadable, categories, og_public, og_groups, date_created, tags, extra_fields, upload_status, notes)
             VALUES( '%s', '%s', '%s', '%s', '%s', '%s', %d, %d, %d, '%s', %d, '%s', '%s', '%s', '%s', '%s', '%s')";
  $query = _asa_format_query($query,
                        $entry_info['title'],
                        $entry_info['body'], 
                        $entry_info['file'],
                        $xmlrpc_server,
                        $user_info['user_name'],
                        $user_info['password'],
                        $entry_info['status'],
                        $entry_info['promote'],
                        $entry_info['downloadable'],
                        serialize($entry_info['categories']),
                        (isset($entry_info['og_public'])?$entry_info['og_public']:TRUE),
                        (isset($entry_info['og_groups'])?serialize($entry_info['og_groups']):serialize(array())),
                        $entry_info['date_created'],
                        serialize($tags),
                        serialize($extra_fields),
                        $upload_status, 
                        $notes);

  $db_result = mysql_query($query, $db_link);

  if (!$db_result) {
    eh_error( 'Could not add audio entry to database: ' . mysql_error());
    return FALSE;
  }

  return TRUE;

}


/*
 * Upload audio entries stored in the queue.
 * 
 * @param $maxcount
 *   integer the maximum number of entries to process per call
 * @return
 *   boolean TRUE indicates success.  If FALSE, check eh_error_msg() for explanation.
 */
function asa_upload_audio_entries_from_queue( $maxcount=1)
{
  global $asa_db_options;

  $uploaded_entries_count = 0;

  // open connection to database server

  $db_link = mysql_connect($asa_db_options['server'], $asa_db_options['user'], $asa_db_options['password']);
  if (!$db_link) {
    eh_error('Could not connect to database server: ' . mysql_error());
    return -1;
  }

  // select database to be used
  $db = mysql_select_db($asa_db_options['db_name'], $db_link);
  if (!$db) {
    eh_error('Can\'t use ' . $asa_db_options['db_name'] . ': ' . mysql_error());
    return -1;
  }
 
  //
  // process the audio entries from the queue
  //


  // select a batch of entries waiting to be processed

  $query = "SELECT entry_id FROM asa_upload_queue WHERE upload_status = '%s' ORDER BY entry_id ASC LIMIT %d";
  $query = _asa_format_query($query, ASA_TO_BE_UPLOADED, $maxcount);
  $db_result = mysql_query($query, $db_link);
  if (!$db_result ) {
    eh_error( 'Could not query audio entries to be uploaded: ' . mysql_error());
    return -1;
  }


  // if there are entries waiting to be uploaded...

  // ... annotate their ids...

  $entry_ids = array();
  while ($entry = mysql_fetch_assoc($db_result)) {
    $entry_ids[] = $entry['entry_id'];
  }


  // ...and attempt to "lock" unprocessed entries so that they are not sent off the queue more than once

  if (!empty($entry_ids)) {
eh_log('cron: there are entries to be uploaded');
    $query = ("UPDATE asa_upload_queue SET upload_status = '%s' WHERE entry_id IN (%s)");
    
    $uniqueid = mt_rand();   // generate a unique id to mark the entries being processed by the current function call
    $entry_ids = implode(', ', $entry_ids);
    $query = _asa_format_query($query, $uniqueid, $entry_ids);
    $db_result = mysql_query($query, $db_link);

    $number_locked = mysql_affected_rows( $db_link);

    if ($number_locked > 0) {

      // get the 'locked' rows ...
      $query = "SELECT * FROM asa_upload_queue WHERE upload_status = '%s'";
      $query = _asa_format_query( $query, $uniqueid);
      $db_result = mysql_query( $query, $db_link);
      if (!$db_result) {
        eh_error( "Error selecting locked audio entries: " . mysql_error());
        return -1;
      }

      // ... and try to upload them to the server
      while ($entry = mysql_fetch_assoc($db_result)) {

        $r = _asa_upload_audio_entry($entry);

        if ($r) {
          $entry['upload_status'] = ASA_UPLOADED;
          $entry['notes'] = "Node id of new entry: $r";

          $uploaded_entries_count++;

        } 
        else {
          // In case of error, add error message as a note in the associated database entry
          $entry['upload_status'] = ASA_ERROR;
          $entry['notes'] = "Error uploading entry to server: " . eh_error_msg();
eh_log("cron: Error uploading entry to server: " . eh_error_msg());
        }

        $entries_processed[] = $entry;
      }


      // now update the status of the processed entries on the database
        
      if (is_array( $entries_processed)) {

        foreach ($entries_processed as $entry) {
          
          $query = "UPDATE asa_upload_queue SET upload_status = '%s', notes = '%s' WHERE entry_id = %d";
          $query = _asa_format_query($query, $entry['upload_status'], $entry['notes'], $entry['entry_id']);
          $db_result = mysql_query($query, $db_link);
          if (!$db_result) {
            eh_error("Error updating database: " . mysql_error());
            return -1;

          }
        
        }
      }
    }

  }


  // return the number of entries uploaded to the server
  if ($uploaded_entries_count > 0) {
    eh_log("Uploaded $uploaded_entries_count entries to the server");
  }

  return $uploaded_entries_count;
}


/*
 * Delete the specified audio entry.
 *
 * @param $xmlrpc_url
 *   string with the URL of the xmlrpc.php file of the associated Drupal server
 *
 * @param $user_info
 *   array with the Drupal name and password of the current user.
 *
 * @param $entry
 *   array with the node id and name of the locally converted file associated with the entry
 *
 * @return
 *   -1 in case of error, -2 in case the user does not have permission to delete entry, 
 *   or 1 in case of success
 */
function asa_delete_audio_entry($xmlrpc_url, $user_info, $entry) {

  global $asa_db_options;

eh_log("asa_delete_audio_entry($xmlrpc_url, $user_info, $entry)");

  //
  // delete audio entry from the server
  //

  $rc = @xmlrpc($xmlrpc_url, 'audio.deleteEntry', $user_info, $entry['nid']);

  if ($rc === FALSE) {       // if xmlrpc call failed...
    // xmlrpc processing error
    eh_error("XMLRPC error deleting audio entry: " . xmlrpc_error_msg());
    return -1;
  }

  if ($rc['error_code'] == -1) {
eh_log("Specified user does not have permission to delete the entry");
    return -2;
  }

  //
  // now delete audio entry from the local cache
  //

  // open connection to database server

  $db_link = mysql_connect($asa_db_options['server'], $asa_db_options['user'], $asa_db_options['password']);

  if (!$db_link) {
    eh_error('Could not connect to database server: ' . mysql_error());
    return -1;
  }

  // select database to be used

  $db = mysql_select_db($asa_db_options['db_name'], $db_link);

  if (!$db) {
    eh_error('Can\'t use ' . $asa_db_options['db_name'] . ': ' . mysql_error());
    return -1;
  }

  if (isset($entry['converted_file'])) {
    // remove file and delete old entry from the cache database
    $result = unlink($entry['converted_file'] . ".wav");

    if (!$result ) {
      eh_error( 'Could not delete old audio file from cache.');
      return -1;
    }

    $converted_file = NULL;

    $query = "DELETE FROM asa_download_cache "
             . " WHERE xmlrpc_url = '%s' AND nid = %d";

    $query = _asa_format_query($query, $xmlrpc_url, $entry['nid']);
    $db_result = mysql_query($query, $db_link);

    if (!$db_result ) {
      eh_error( 'Could not query audio download cache: ' . mysql_error());
      return -1;
    }
  }
  return 1;
}


/*
 * Retrieve the information associated with the audio entries that obey the specified criteria.
 *
 * @param $xmlrpc_url
 *   URL of the xmlrpc.php file associated with the audio Drupal server.
 *
 * @param $user_info
 *   array with the Drupal name and password of the user.
 *
 * @param $query_info
 *   array with the query criteria for the audio entries:
 *     ['status'] :: 1 = published, 0 = unpublished, NULL = any
 *     ['promote'] :: 1 = promoted to the front page, 0 = not promoted, NULL = any
 *     ['uid'] :: user id of the desired entries author, or NULL for any
 *     ['categories_or'] :: array with category ids to be ORed; or NULL for all
 *     ['categories_and'] :: array with category ids to be ANDed; or NULL for all
 *     ['unread_entries_only'] :: TRUE, return only the entries that haven't been read by the user
 *
 * @return
 *   mixed value. In case of success, returns array with number of entries and the
 *   meta-information about the entries themselves.  In case of failure, returns
 *   FALSE; check eh_error_msg() for explanation.
 */

function asa_retrieve_meta_audio_entries($xmlrpc_url, $user_info, $query_info) {
eh_log("Entering asa_retrieve_meta_audio_entries($xmlrpc_url, $user_info, $query_info)");

  $query_info['get_bodies']  = TRUE;
  $query_info['get_audio_files']  = FALSE;    // the file itself will only be downloaded when the entry is played

  // send request to server

  $rc = @xmlrpc($xmlrpc_url, 'audio.getRecentEntries', $user_info, $query_info);

  if ($rc === FALSE) { // xmlrpc error
    eh_error("XMLRPC error retrieving audio entries: " . xmlrpc_error_msg());
    return FALSE;
  }

  if ($rc['error_code'] === -1) {
    eh_error("Error retrieving audio entries: {$rc['error_msg']}");
    return FALSE;
  }

  return $rc;
}

/*
 * Retrieve list of categories available for audio entries
 *
 * @param $xmlrpc_url
 *   URL of the xmlrpc.php file associated with the audio Drupal server.
 *
 * @return
 *   mixed value. In case of success, returns array with categories associated with
 *   audio entries.  In case of failure, returns FALSE; check eh_error_msg() for explanation.
 */
function asa_get_audio_category_list($xmlrpc_url) {

  $rc = @xmlrpc($xmlrpc_url, 'audio.getCategoryList');

  if ($rc !== FALSE) {       // if xmlrpc call successful...
      return $rc;     // return an array with the categories
  }
  else {
    eh_error("XMLRPC error retrieving audio category list: " . xmlrpc_error_msg());
    return FALSE;
  }
}

/*
 * Check if user can performed specified operation on the given audio node.
 *
 * @param $xmlrpc_url
 *   URL of the xmlrpc.php file associated with the audio Drupal server.
 * 
 * @param $user_info
 *   array with the user name and password of the current user
 *
 * @param $op 
 *   string with the operation to be performed on the node ('view', 'update', 'delete', 'create')
 * 
 * @param $nid
 *   The node id of the audio entry to be checked
 * 
 * @return
 *   array. In case of success, 'error_code' will be set to 1 and 'allowed' will contain the result of the checking.
 *   In case of failure, 'error_code' will be set to -1 and 'error_msg' will contain an explanation of the error.
 */
function asa_check_access($xmlrpc_url, $user_info, $op, $nid) {
//$xmlrpc_url .= '?XDEBUG_SESSION_START=whatsup';
  $rc = xmlrpc($xmlrpc_url, 'audio.checkAccess', $user_info, $op, (int)$nid);
eh_log('xmlrpc result: ' . print_r($rc, TRUE));  
  if ($rc === FALSE) {       // if xmlrpc call unsuccessful...
    eh_error("XMLRPC error checking access permissions: " . xmlrpc_error_msg());
    $rc = array();
    $rc['error_code'] = -1;
    $rc['error_msg'] = "Processing failure. XMLRPC error checking access permissions: " . xmlrpc_error_msg();
  }
  return $rc;
}

/*
 * Download the specified audio entry.
 *
 * @param $xmlrpc_url
 *   URL of the xmlrpc.php file associated with the audio Drupal server.
 * 
 * @param $user_info
 *   array with the user name and password of the current user
 *
 * @param $query_info
 *   array with the query criteria that uniquely identifies the entry
 *     ['nid'] :: the node id of the audio entry
 *     ['get_body'] :: TRUE, include the body of the blog entry associated with the audio entry
 *     ['get_audio_file'] :: TRUE, include the audio file associated with the entry
 *
 * @return
 *   mixed value. In case of success, returns array with the specified audio entry.  
 *   In case of processing failure, returns -1; check eh_error_msg() for explanation.
 *   Returns 0 in case the user is not allowed to retrieve the specified audio entry.
 */
function asa_download_specific_entry($xmlrpc_url, $user_info, $query_info) {
eh_log("Calling xmlrpc($xmlrpc_url, 'audio.getEntry', $user_info, $query_info)");
eh_log('user_info: ' . print_r($user_info, TRUE));
eh_log('query_info: ' . print_r($query_info, TRUE));
  $rc = xmlrpc($xmlrpc_url, 'audio.getEntry', $user_info, $query_info);
// Note: this is too big to be displayed!! eh_log('value returned from getEntry: ' . print_r($rc, TRUE));
eh_log("rc: $rc");
  if ($rc === FALSE) {       // if xmlrpc call unsuccessful...
   eh_error("XMLRPC error downloading audio entry: " . xmlrpc_error_msg());
    return -1;
  }
eh_log('success, so far. error_code: ' . $rc['error_code']);  
  if ($rc['error_code'] == 1) {
    return $rc['entry'];     // return array with the audio entry
  }
  else if ($rc['error_code'] == 0) {
  	return 0;
  }
  else {
    eh_error("Processing error downloading audio entry: {$rc['error_msg']}");
    return -1;
  }
}

/*
 * Update access statistics associated with the specified audio entry.
 */
function asa_update_audio_entry_statistics($xmlrpc_url, $user_info, $nid_entry) {

eh_log("Entering asa_update_audio_entry_statistics($xmlrpc_url, $user_info, $nid_entry)");
  $rc = xmlrpc($xmlrpc_url, 'audio.updateAccessStatistics', $user_info, $nid_entry);

  if ($rc === FALSE) {
    // xmlrpc error
    eh_error("XMLRPC error updating access statistics for node $nid_entry: " . xmlrpc_error_msg());
    return FALSE;
  }

  if ($rc === 1) {
    return TRUE;
  }
  else {
    eh_error("Error updating access statistics for node $nid_entry.");
    return FALSE;
  }
}

/*
 * Return the audio file associated with the specified audio node id and keep a copy in the local cache
 * 
 * @param $xmlrpc_url
 * string with the URL of the xmlrpc.php file of the server where the desired audio node is stored
 *  
 * @param $user_info
 * array with user name, password of the current user
 * 
 * @param $entry
 * array with the id (and optional modified date) of the desired audio node
 * 
 * @return
 * string with the base name of the file that contains the audio (without the suffix).  
 * In case of processing error, returns -1 and set eh_error_msg() to the appropriate error message.
 * Returns 0 in case the user is not allowed to access the specified audio entry.
 */
function asa_get_audio_file($xmlrpc_url, $user_info, $entry) {

  global $asa_tmp_dir;
  global $asa_db_options;

eh_log("Entering asa_get_audio_file: $xmlrpc_url, user_info: " . print_r($user_info, TRUE) . ", entry: " . print_r($entry, TRUE));

  $nid = $entry['nid'];
  $last_modified = isset($entry['last_modified'])?$entry['last_modified']:0;
  
  // Check if the caller has the necessary permissions to access the specified audio entry
  $rc = asa_check_access($xmlrpc_url, $user_info, 'view', $nid);
  if ($rc['error_code'] == -1) {
  	eh_error('Processing failure checking node access: ' . $rc['error_msg']);
  	return -1;
  }
  else if (($rc['error_code'] == 1) && !$rc['allowed']) {
    return 0;
  }

  //
  // Check if requested file is in the cache already
  //

  // open connection to database server
  $db_link = mysql_connect($asa_db_options['server'], $asa_db_options['user'], $asa_db_options['password']);
  if (!$db_link) {
    eh_error('Could not connect to database server: ' . mysql_error());
    return -1;
  }

  // select database to be used
  $db = mysql_select_db($asa_db_options['db_name'], $db_link);
  if (!$db) {
    eh_error('Can\'t use ' . $asa_db_options['db_name'] . ': ' . mysql_error());
    return -1;
  }

  // select the audio file name from the cache database
  $query = "SELECT converted_file, last_modified FROM asa_download_cache "
           . " WHERE xmlrpc_url = '%s' AND nid = %d";
  $query = _asa_format_query($query, $xmlrpc_url, $nid);

  $db_result = mysql_query($query, $db_link);

  if (!$db_result ) {
    eh_error( 'Could not query audio download cache: ' . mysql_error());
    return -1;
  }

  $cache_entry = mysql_fetch_assoc($db_result);

  $converted_file = NULL;

  //
  // If file exists in the cache...
  //

  if ($cache_entry) { 
eh_log("Requested file is already in cache");

    $converted_file =& $cache_entry['converted_file'];

    // if requested file is newer than cached one...
    if ($cache_entry['last_modified'] < $last_modified) {

      // remove file and delete old file from the cache database
eh_log("Deleting old audio file from cache");
      $result = unlink($converted_file . ".wav");
      if (!$result ) {
        eh_error( "Processing error. Could not delete file $converted_file.wav from cache.");
        return -1;
      }

      $converted_file = NULL;

      $query = "DELETE FROM asa_download_cache "
               . " WHERE xmlrpc_url = '%s' AND nid = %d";
      $query = _asa_format_query($query, $xmlrpc_url, $nid);
      $db_result = mysql_query($query, $db_link);
      if (!$db_result ) {
        eh_error( 'Could not query audio download cache: ' . mysql_error());
        return -1;
      }
    }
    else {
      // if cached file is up-to-date, update its last_accessed date.  This date is used as the basis for asa_cache_recycling().

      $query = "UPDATE asa_download_cache SET last_accessed = %d "
               . " WHERE xmlrpc_url = '%s' AND nid = %d";
      $query = _asa_format_query($query, time(), $xmlrpc_url, $nid);
      $db_result = mysql_query($query, $db_link);
      if (!$db_result) {
        eh_error("Error updating last_accessed field in audio file cache: " . mysql_error());
        return -1;
      }
    }
  }
  

  //
  // Download and convert new audio file from the server
  //

  if (!$converted_file) {
    require_once('voip_io.inc');

    $query_info = array();
    $query_info['nid'] = $nid;
    $query_info['get_body'] = FALSE;
    $query_info['get_audio_file'] = TRUE;

    $entry = @asa_download_specific_entry($xmlrpc_url, $user_info, $query_info);
    if (!$entry) {
      // error processing download request
      return -1;
    }

    //
    // Convert file to 8KHz .wav to be played to user
    //

    // create a temporary file to store the downloaded information
    $base_file = tempnam($asa_tmp_dir, 'asa_');

    $fp = fopen("$base_file", 'wb');
    if (!$fp) {
      eh_error("Temporary file $base_file could not be created.");
      return -1;
    }
    fwrite($fp, $entry['file_info']['bits']);
    fclose($fp);

    // determine the mime type of the downloaded information
    $mime_type = $entry['file_info']['mime_type'];
    $valid_mime_types = array("audio/x-wave", "audio/mpeg");
    if (!in_array($mime_type, $valid_mime_types)) {
      $original_mime_type = $mime_type;
      // try to determine locally
      $mime_type = mime_content_type($base_file);
      if (!in_array($mime_type, $valid_mime_types)) {
        eh_error("Invalid mime type: $original_mime_type");
        unlink($base_file);
        return -1;
      }
      eh_log("Invalid mime type: $original_mime_type being replaced by new type: $mime_type");
    }

    // convert the file to an Asterisk-compatible wav file
    $converted_file = tempnam($asa_tmp_dir, 'asa_');  
    rename($converted_file, $converted_file . '.wav');
    _asa_convert_sound_file_from_download($base_file, $mime_type, $converted_file . '.wav');

    // delete the downloaded file
    unlink($base_file);

    //
    // Add converted file to the download cache
    //

    $last_accessed = time();
    $query = "INSERT INTO asa_download_cache
                (xmlrpc_url, nid, converted_file, last_modified, last_accessed)
                 VALUES ('%s', %d, '%s', %d, %d)";
    $query = _asa_format_query($query, $xmlrpc_url, $nid, $converted_file, $entry['last_modified'], $last_accessed);
    $db_result = mysql_query($query, $db_link);
    if (!$db_result) {
      eh_error( 'Could not add entry to audio cache database: ' . mysql_error());
      return -1;
    }
  }

  // TODO: close database

  return $converted_file;   // Note that converted_file name does not have a suffix...
}


/*
 * Delete old entries stored in the download cache
 *
 */
function asa_cache_recycle($recycle_days)
{
  global $asa_db_options;

  // open connection to database server

  $db_link = mysql_connect($asa_db_options['server'], $asa_db_options['user'], $asa_db_options['password']);

  if (!$db_link) {
    eh_error('Could not connect to database server: ' . mysql_error());
    return FALSE;
  }

  // select database to be used

  $db = mysql_select_db($asa_db_options['db_name'], $db_link);

  if (!$db) {
    eh_error('Can\'t use ' . $asa_db_options['db_name'] . ': ' . mysql_error());
    return FALSE;
  }

  //
  // remove all the audio files that have been downloaded more than recycle_days ago
  //

  // select a batch of entries waiting to be processed

  $recycle_limit = time() - $recycle_days * 86400;  // number of seconds in a day

  $query = "SELECT cid, converted_file FROM asa_download_cache WHERE last_accessed < %d ";
  $query = _asa_format_query($query, $recycle_limit); 

  $db_result = mysql_query($query, $db_link);

  if (!$db_result ) {

    eh_error( 'Could not query audio entries to be uploaded: ' . mysql_error());
    return FALSE;
  }

  // delete all the selected entries and their associated files

  while ($entry = mysql_fetch_assoc($db_result)) {

    if (!$entry) {
      print (mysql_error());
      return;
    }

    $file = $entry['converted_file'] . '.wav';
eh_log("recycling: about to delete $file");
    if (file_exists($file)) {    
      $result = unlink($file);

      if (!$result ) {
        eh_error( "recycling: Could not recycle old audio file from cache: $file");
        return FALSE;
      }
eh_log("recycling: deleted: $file");
    }

    $query = "DELETE FROM asa_download_cache WHERE cid = %d";

    $query = _asa_format_query($query, $entry['cid']);
eh_log("recycling: about to delete db entry: " . print_r($entry, TRUE));
    $db_result2 = mysql_query($query, $db_link);

    if (!$db_result2) {
      eh_error( 'recycling: Could not query audio cache for recycling: ' . mysql_error());
      return FALSE;
    }
  }

  return TRUE;

}


// -----------------------------------------------------------------------------
// private function definitions
// -----------------------------------------------------------------------------

/*
 * Upload the specified audio entry to the server.
 * 
 * @param $entry
 *   array with all the necessary parameters for the uploading
 * @return
 *   integer the id of the newly created blog entry (nid).  In case of error, 
 *          returns FALSE and stores the error description on eh_error_msg().
 */
function _asa_upload_audio_entry($entry) {
eh_log('uploading entry: ' . print_r($entry, TRUE));
// TODO: What to do with the date_created field from the database?  Can it be used to set node->created in the server?

  // prepare file to be uploaded
  $mime_type = NULL;
  $out_file = NULL;
  $ret = _asa_convert_sound_file_to_upload($entry['file'], $out_file, $mime_type);
  if (!$ret) {
    eh_log("Error preparing file {$entry['file']} to be uploaded");
    return FALSE;
  }

  $fh = fopen($out_file, 'rb');
  if (!$fh) {
    eh_error( "Error opening file: $out_file");
    return FALSE;
  }
  $file_info['bits'] = "";     // doing this just to prevent PHPAGI from emailing me a warning message after the 'while' below...
  while (!feof($fh)) {
    $file_info['bits'] .= fread( $fh, 8192);
eh_log('saving bits');
  }
  $file_info['name'] = basename($out_file);
eh_log("file name: {$file_info['name']}, count: " . strlen($file_info['bits']));
  $file_info['bits'] = xmlrpc_base64($file_info['bits']);

  // prepare the content of the audio node to be processed by the server
  
  $user_info = array();
  $user_info['user_name'] = $entry['user_name'];
  $user_info['password'] = $entry['password'];

  $entry_info = $entry;
  $entry_info['categories'] = unserialize($entry['categories']);
  $entry_info['og_public'] = $entry['og_public'];
  $entry_info['og_groups'] = unserialize($entry['og_groups']);
  $tags = unserialize($entry['tags']);
  $entry_info['tags'] = is_null($tags)?array():$tags;
  $entry_info['tags']['name'] = $file_info['name'];
  $entry_info['tags']['mime_type'] = $mime_type;
  $entry_info['extra_fields'] = unserialize($entry['extra_fields']);
eh_log('entry_info: ' . print_r($entry_info, TRUE));

  // create a new entry with the uploaded information
  $rc = @xmlrpc($entry['xmlrpc_url'],
          'audio.createEntry',
          $user_info,
          $entry_info,
          $file_info
         );

eh_log("result from create entry: " .  print_r($rc, TRUE));

  if ($rc === FALSE) {       // if xmlrpc call fails...
    eh_error("XMLRPC error creating audio entry: " . xmlrpc_error_msg());
    return FALSE;
  }
eh_log('error_code: ' . print_r($rc['error_code'], TRUE));
if ($rc['error_code'] != 1) 
  { eh_log("(rc['error_code'] != 1)");};
if ($rc['error_code'] !== 1) eh_log("(rc['error_code'] !== 1)");  

  if ($rc['error_code'] !== 1) {
    eh_error("Server error creating audio entry: {$rc['error_msg']}'");
    return FALSE;
  } 
  	
  //
  // store original file in the download cache so that it does not have to be converted again
  //
  
  // open connection to database server
  
  global $asa_db_options;
  global $asa_tmp_dir;
  
  $db_link = mysql_connect($asa_db_options['server'], $asa_db_options['user'], $asa_db_options['password']);

  if (!$db_link) {
    eh_error('Could not connect to database server: ' . mysql_error());
    return FALSE;
  }

  // select database to be used

  $db = mysql_select_db($asa_db_options['db_name'], $db_link);

  if (!$db) {
    eh_error('Can\'t use ' . $asa_db_options['db_name'] . ': ' . mysql_error());
    return FALSE;
  }
  
  // rename and move file so that it becomes compatible with other downloaded files
  $converted_file = $asa_tmp_dir . DIRECTORY_SEPARATOR . basename($entry['file'], '.wav');
  $renamed = rename($entry['file'], $converted_file . '.wav');
  if (!$renamed) {
    eh_error("Could not rename {$entry['file']} to $converted_file");
    return FALSE;
  }
  
  $last_accessed = time();

  $query = "INSERT INTO asa_download_cache
            (xmlrpc_url, nid, converted_file, last_modified, last_accessed)
             VALUES ('%s', %d, '%s', %d, %d)";
  $query = _asa_format_query($query, $entry['xmlrpc_url'], $rc['nid'], $converted_file, $rc['last_modified'], $last_accessed);

  $db_result = mysql_query($query, $db_link);

  if (!$db_result) {
    eh_error( 'Could not add entry to audio cache database: ' . mysql_error());
    return FALSE;
  }

  // --------------

  // delete the local copy of the uploaded file
  $file_removed = unlink($out_file);
  if (!$file_removed) {
    eh_error("Error removing uploaded file");
    return FALSE;
  }

  return $rc['nid'];     // return the node id of the new audio entry

}


/*
 * Formats the arguments for a database query
 * Note: this function is based on Drupal's db_query function
 * 
 * @param
 *   string query A string containing an SQL query.
 * @param
 *   ... A variable number of arguments which are substituted 
 *       into the query using printf() syntax. Instead of a 
 *       variable number of query arguments, you may also pass
 *       a single array containing the query arguments.
 * @return
 *   string with the new query arguments formatted
 */
function _asa_format_query($query) {
  $args = func_get_args();
  if (count($args) > 1) {
    if (is_array($args[1])) {
      $args = array_merge(array($query), $args[1]);
    }                
    $args = array_map('_asa_db_escape_string', $args);
    $args[0] = $query;
    $query = call_user_func_array('sprintf', $args);
  }                              
  return $query;        
}                                   

/*
 * Callback function called by _asa_format_query. 
 *    It inserts backslashes in the input argument in such a way
 *    that it becomes appropriate to be used in a database query.
 * 
 * @param
 *   string text to be modified
 * @return
 *   string the modified text
 */
function _asa_db_escape_string($text) {
  return addslashes($text);
}  

/**
 * Converts an mp3 or regular wav file to an Asterisk compatible wav file
 * 
 * @param $orig_file
 *   Fullpath to the original file
 * @param $orig_filemime
 *   The mimetype of the original file
 * @param $converted_file
 *   (Desired) Fullpath to the converted file
 * @return
 *   The return value of the conversion program (mpg123 or sox)
 */
function _asa_convert_sound_file_from_download($orig_file, $orig_filemime, $converted_file) {
  global $asa_tmp_dir, $asa_sox, $asa_mpg123;

  // Try to detect an mp3 file. If it is an mp3 file, then convert it to a wav
  // We don't make the wav directly in case we are actually using mpg321
  // (which doesn't have all these options)
  //system("$asa_mpg123 -q --mono -r 8000 -f 4096 -w $temp_file $orig_file", $ret);
  if (strpos($orig_filemime, 'mp') !== FALSE) {
    $temp_file = tempnam($asa_tmp_dir, 'vu_download_');
    system("$asa_mpg123 -q -m -w '$temp_file' '$orig_file'", $ret);
    $orig_file = $temp_file;
  }
  // We have a wav file, convert it to an Asterisk compatible wav file
  system(escapeshellcmd("$asa_sox '$orig_file' -r 8000 -c 1 '$converted_file'"), $ret);

  // If there is a temp file, delete it
  if (isset($temp_file)) {
    unlink($temp_file);
  }
  return $ret;
}

/**
 * Converts a input wav file to a type appropriate for being uploaded to the site (mp3)
 * 
 * @param $original_file
 *   Fullpath to the original file
 *
 * @param $file_out
 *   Set to the full path to the converted file
 * 
 * @param $mime_type
 *   set to the mime type of the converted file
 * 
 * @return
 *   FALSE in case of error.
 */
function _asa_convert_sound_file_to_upload($in_file, &$out_file, &$mime_type) {
  global $asa_tmp_dir, $asa_lame;

  // create new file name
  $out_file = tempnam($asa_tmp_dir, 'vu_upload_');
  if (!$out_file) {
  	eh_error('Error creating temporary file.');
  	return FALSE;
  }
  
  // convert file to .mp3
  // Note: Due to a bug, Flash can only play MP3 files with either 11, 22 or 44 kHz (check LAME forum).  
  //       The only problem is that resampling audio files takes a long time...
  // TODO: add ID3 information to new MP3 file? Use lame --tt --ta --tl --tc --tg etc.

eh_log("String to be processed: " . escapeshellcmd("$asa_lame --quiet --resample 11 '$in_file' '$out_file' "));
  system(escapeshellcmd("$asa_lame --quiet --resample 11 '$in_file' '$out_file' "));
  $mime_type = 'audio/mpeg';
  
  // add a .mp3 suffix to file name
  $r = rename($out_file, $out_file . '.mp3');
  if (!$r) {
  	eh_error("Error renaming file: $out_file.");
  	return FALSE;
  }
  $out_file = $out_file . '.mp3';
  
  return TRUE;
  
}

function asa_touch_node($xmlrpc_url, $nid) {

  global $asa_db_options;

  // open connection to database server
  $db_link = mysql_connect($asa_db_options['server'], $asa_db_options['user'], $asa_db_options['password']);
  if (!$db_link) {
    eh_error('Could not connect to database server ' . $asa_db_options['server'] . ': ' . mysql_error());
    return FALSE;
  }

  // select database to be used
  $db = mysql_select_db($asa_db_options['db_name'], $db_link);
  if (!$db) {
    eh_error('Can\'t use ' . $asa_db_options['db_name'] . ': ' . mysql_error());
    return FALSE;
  }

  // update its last_accessed date of the specified node.  This date is used as the basis for asa_cache_recycling().
  $query = "UPDATE asa_download_cache SET last_accessed = %d "
           . " WHERE xmlrpc_url = '%s' AND nid = %d";
  $query = _asa_format_query($query, time(), $xmlrpc_url, $nid);
  $db_result = mysql_query($query, $db_link);
  if (!$db_result) {
    eh_error("Error updating last_accessed field in audio file cache: " . mysql_error());
    return FALSE;
  }
  
  return TRUE;
}

?>
