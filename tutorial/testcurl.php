<?php
$curl = curl_init();
//设置抓取的url
curl_setopt($curl, CURLOPT_URL, 'http://localhost:8080/');
//设置头文件的信息作为数据流输出
//curl_setopt($curl, CURLOPT_HEADER, 1);
//设置获取的信息以文件流的形式返回，而不是直接输出。
curl_setopt($curl, CURLOPT_RETURNTRANSFER, 1);
//执行命令
$data = curl_exec($curl);
//关闭URL请求
curl_close($curl);
//显示获得的数据

 echo "OK了，,ssss";

 echo "<br/>";

var_dump($data);
 echo "<br/>";
 echo microtime();

?>
