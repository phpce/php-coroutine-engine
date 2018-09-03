<?php
 // echo "OK了，,ssss";
echo "context->thread_id:".ce_context_index()."<br/>\n";
echo "context->fd:".ce_context_fd()."<br/>\n";
 // echo "<br/>";
 // var_dump(ce_http_get("http://localhost:8080/"));
 var_dump(coro_http_get("http://10.60.198.153:8080/"));
 //var_dump(coro_http_get("http://localhost:8080/"));
 // var_dump(coro_http_get("http://live.ksmobile.net/base/apiend"));
 // echo "<br/>";
 // echo microtime();
 // echo "<br/>context->thread_id:".ce_context_index()."<br/>\n";
?>
