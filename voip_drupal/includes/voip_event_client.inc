<?php
/* $Id: voip_event_client.inc,v 1.6 2006/11/01 12:03:22 leob Exp $ */

/**
 * @file
 *   Basic functions to handle voip events.
 */


// -----------------------------------------------------------------------------
// include required files
// -----------------------------------------------------------------------------

foreach (array( 'error_handler.inc', 'voip_client.inc', 'xmlrpc.inc') as $file) {
   require_once($file);
}


// -----------------------------------------------------------------------------
// constants
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// global variables
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// initialization
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// public function definitions
// -----------------------------------------------------------------------------

/*
 * Create a voip event. 
 * 
 * NOTE: It may take a while for audio files to be uploaded.  The caller module has to keep checking extension updates from time to time.
 */
function voip_event_create_event($xmlrpc_server, $user_info, $event_info, $file_audio_name = NULL, $file_audio_description = NULL, $file_audio_location = NULL) {
eh_log("voip_event_create_event($xmlrpc_server, $user_info, $event_info, $file_audio_name)");
eh_log("user_info: " . serialize($user_info));
eh_log("event_info: " . print_r($event_info, TRUE));

  $rc = array('error_code' => VOIP_SUCCESS);
//$xmlrpc_server .= '?XDEBUG_SESSION_START=whatsup';
  // create the basic voip event  
  $result = xmlrpc($xmlrpc_server, 'voip_event.createEvent', $user_info, $event_info);
eh_log("XML-RPC voip_event.createEvent returned: " . serialize($result));
  if (!$result) {
    eh_error("XMLRPC error:" . xmlrpc_error_msg());
    $rc['error_code'] = VOIP_PROCESSING_FAILURE;
    $rc['error_msg'] = "XMLRPC error:" . xmlrpc_error_msg();
    return $rc;
  }

  if ($result['error_code'] !== VOIP_SUCCESS) {
    eh_error("Error creating voip_event: {$result['error_msg']}");
    return $result;
  }

  // Now upload the audio name associated with the event
  $extension_info = $result['extension_info'];
  if ($file_audio_name) {
    $rc = voip_extension_set_audio_field($xmlrpc_server, $user_info, $extension_info['number'], 'audio_name', $file_audio_name);
     if ($rc['error_code'] !== VOIP_SUCCESS) {
       return $rc;
     }
  }

  // Upload the audio description associated with the event
  if ($file_audio_description) {
    $rc = voip_extension_set_audio_field($xmlrpc_server, $user_info, $extension_info['number'], 'audio_description', $file_audio_description);
     if ($rc['error_code'] !== VOIP_SUCCESS) {
       return $rc;
     }
  }
  
  // Upload the audio file associated with the event location
  $audio_key = $result['voip_event_audio_key'];
eh_log("audio_key: $audio_key");

  $audio_info['title'] = "Audio location for event extension " . $extension_info['number'];
  $audio_info['body'] = '';
  $audio_info['file'] = $file_audio_location;
  $audio_info['status'] = 0;
  $audio_info['promote'] = FALSE;
  $audio_info['downloadable'] = FALSE;
  $audio_info['categories'] = array();
  $audio_info['date_created'] = time();
  $audio_info['extra_fields'] = array( 'voip_event_audio_key' => $audio_key);

  // add the audio file to the queue to be uploaded to the server
  $r = asa_add_audio_entry_to_upload_queue($xmlrpc_server, $user_info, $audio_info);
eh_log("Value returned by asa_add_audio_entry_to_upload_queue: $r");
  if (!$r) {
    $rc['error_code'] = VOIP_PROCESSING_FAILURE;
    $rc['error_msg'] = "Error adding audio location to upload queue:" . eh_error_msg();
    return $rc;
  }
  else {
    $rc['error_code'] = VOIP_SUCCESS;
  }

  return $result;
}

/*
 * Determine whether event times should be input in 12h- or 24h-format 
 */
function voip_event_get_ampm_flag($xmlrpc_server) {
eh_log("voip_event_get_ampm_flag($xmlrpc_server)");

  $result = xmlrpc($xmlrpc_server, 'voip_event.getAmpmFlag');
eh_log("XML-RPC voip_event.getAmpmFlag returned: " . serialize($result));
  if (!$result) {
    eh_error("XMLRPC error:" . xmlrpc_error_msg());
    $rc['error_code'] = VOIP_PROCESSING_FAILURE;
    $rc['error_msg'] = "XMLRPC error:" . xmlrpc_error_msg();
    return $rc;
  }
  return $result;
}

/*
 * Delete a voip event. 
 */
function voip_event_delete_event($xmlrpc_server, $user_info, $extension_info) {
  // delete the event extension
  $r = voip_extension_delete_node_extension($xmlrpc_server, $user_info, $extension_info);
  switch ($r['error_code']) {
    case VOIP_SUCCESS:
      // now delete the audio files associated with the extension's name, greetings and description
      $audio_fields = array('file_audio_location');
      foreach($audio_fields as $audio_field) {
        if (isset($extension_info[$audio_field])) {
        unlink($extension_info[$audio_field] . ".wav");
        }
      }
      return $r;
    case VOIP_LACK_OF_PERMISSION:
      return $r;
    case VOIP_PROCESSING_FAILURE:
    default:
eh_log('Error processing extension delete: ' . serialize($r));
      $rc = array();
      $rc['error_code'] = VOIP_PROCESSING_FAILURE;
      $rc['error_message'] = (isset($r['error_msg']))?$r['error_msg']:('Error processing event delete: ' . serialize($r));  
      return $rc;
  }
  return $rc;
}

/*
 * Retrieve event associated with the specified node id.
 */
function voip_event_get_event_from_nid($xmlrpc_server, $user_info, $nid) {
  
  $result = xmlrpc($xmlrpc_server, 'voip_event.getEventInfoFromNid', $user_info, (int)$nid);
  if (!$result) {
    eh_error("XMLRPC error:" . xmlrpc_error_msg());
    $rc = array();
    $rc['error_code'] = VOIP_PROCESSING_FAILURE;
    $rc['error_msg'] = "XMLRPC error:" . xmlrpc_error_msg();
    return $rc;
  }

  if ($result['error_code'] != VOIP_SUCCESS) {
    eh_error("Error retrieving event with nid $nid: {$result['error_msg']}");
    return $result;
  }

  $event_info = $result['event_info'];
  $date = $event_info['start'];
  $ts_start = mktime($date['hour'], $date['minute'], $date['second'], $date['month'], $date['day'], $date['year']);
  $ts_start_day = mktime(0, 0, 0, $date['month'], $date['day'], $date['year']);
  $date = $event_info['end'];
  $ts_end = mktime($date['hour'], $date['minute'], $date['second'], $date['month'], $date['day'], $date['year']);
  $ts_end_day = mktime(0, 0, 0, $date['month'], $date['day'], $date['year']);
  $r = _voip_event_get_event_info_files($xmlrpc_server, $user_info, $event_info);
  if ($r['error_code'] != VOIP_SUCCESS) {
    return $r;
  }
  $event_info = $r['event_info'];
  $event_info['ts_start'] = $ts_start;
  $event_info['ts_end'] = $ts_end;
  $event_info['kind'] = ($ts_start_day == $ts_end_day) ? 'single_day' : 'multi_day';
      
  $rc = array();
  $rc['error_code'] = VOIP_SUCCESS;
  $rc['event_info'] = $event_info;

  return $rc;
}


/*
 * Retrieve events that start/end within the specified time frame.
 * 
 * NOTE: this function does not retrieve all the audio files associated with the events, only the descriptors
 */
function voip_event_get_events($xmlrpc_server, $user_info, $start, $end = NULL, $limit = 5, $terms = NULL, $organizer_extension_number = NULL, $gid = NULL) {
  
  $result = xmlrpc($xmlrpc_server, 'voip_event.getEvents', $user_info, $start, $end, (int)$limit, $terms, (int) $organizer_extension_number, (int)$gid);

  if (!$result) {
    eh_error("XMLRPC error:" . xmlrpc_error_msg());
    $rc = array();
    $rc['error_code'] = VOIP_PROCESSING_FAILURE;
    $rc['error_msg'] = "XMLRPC error:" . xmlrpc_error_msg();
    return $rc;
  }

  if ($result['error_code'] != VOIP_SUCCESS) {
    eh_error("Error retrieving events: {$result['error_msg']}");
    return $result;
  }

  // for each of the events retrieved...
  $rc['event_list'] = array();
  foreach($result['events'] as $event_info) {
      $r = _voip_event_get_event_info_files($xmlrpc_server, $user_info, $event_info);
      if ($r['error_code'] != VOIP_SUCCESS) {
        return $r;
      }
      $event_info = $r['event_info'];
      $date = $event_info['start'];
      $ts_start = mktime($date['hour'], $date['minute'], $date['second'], $date['month'], $date['day'], $date['year']);
      $ts_start_day = mktime(0, 0, 0, $date['month'], $date['day'], $date['year']);
      $date = $event_info['end'];
      $ts_end = mktime($date['hour'], $date['minute'], $date['second'], $date['month'], $date['day'], $date['year']);
      $ts_end_day = mktime(0, 0, 0, $date['month'], $date['day'], $date['year']);
      $event_info['ts_start'] = $ts_start;
      $event_info['ts_end'] = $ts_end;
      $event_info['kind'] = ($ts_start_day == $ts_end_day) ? 'single_day' : 'multi_day';
      $rc['event_list'][] = $event_info;
  } 
  $rc['error_code'] = VOIP_SUCCESS;
  return $rc;
}


function _voip_event_get_event_info_files($xmlrpc_server, $user_info, $event_info) {

  // (20080609) $event_extension_number = $event_info['event_extension_number'];
  $event_extension_number = $event_info['number'];
	
  // get the event extension info (with associated audio files) 
  $r = voip_extension_get_info($xmlrpc_server, $user_info, $event_extension_number);
  if ($r['error_code'] != VOIP_SUCCESS) {
    return $r;
  }
  $extension_info = $r['extension_info'];

  $event_info['extension_info'] = $extension_info;
  
  // get the audio recording of the event location
  if ($event_info['nid_audio_location']) {
    $basename = asa_get_audio_file($xmlrpc_server, $user_info, array('nid' => $event_info['nid_audio_location']));
    if (($basename == -1) || ($basename === 0)) {
      $rc['error_code'] = VOIP_PROCESSING_FAILURE;
      $rc['error_msg'] =  "Error retrieving audio location file: " . eh_error_msg();
      return $rc;
    }  
    // create a new entry in extension_info for the file
    $event_info['file_audio_location'] = $basename;
  }
    
  // get the extension info of the event organizer
  if (isset($event_info['organizer_extension_number'])) {
    // (20080609) $organizer_extension_number = $event_info['organizer_extension_number'];
    $organizer_extension_number = $event_info['author_extension_number'];
    $r = voip_extension_get_info($xmlrpc_server, $user_info, $organizer_extension_number);
    if ($r['error_code'] != VOIP_SUCCESS) {
      return $r;
    }
    $event_info['organizer_extension_info'] = $r['extension_info'];
  }
    
  $rc = array();
  $rc['error_code'] = VOIP_SUCCESS;
  $rc['event_info'] = $event_info;
  return $rc;
}

// -----------------------------------------------------------------------------
// private function definitions
// -----------------------------------------------------------------------------


?>
