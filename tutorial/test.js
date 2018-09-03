//引入http模块
var http = require("http");
//设置主机名
var hostName = '0.0.0.0';
//设置端口
var port = 8080;
//创建服务
var server = http.createServer(function(req,res){
    res.setHeader('Content-Type','text/plain');
	setTimeout(function(){
    	res.end("hello nodejs");
	},2000);

});
server.listen(port,hostName,function(){
    console.log(`服务器运行在http://${hostName}:${port}`);
});
