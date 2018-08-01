<?php
 echo "libevent 终于OK了，,ssss";
 echo $_SERVER['coroutine_time'];

 echo "<br/>";
// phpinfo();
//sleep(5);
 // $a = 1;
 var_dump(coro_http_get("http://live.ksmobile.net/base/apiend"));
 echo "<br/>";
 echo microtime();
 // echo "[".$a."]";
?>
