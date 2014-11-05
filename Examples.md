
Match errors with requests.

    SELECT 
      a.remote_host, a.status, e.message, a.url 
    FROM
      access_log a, error_log e 
    WHERE a.time_epoch = e.time_epoch;