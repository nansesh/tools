var program = require('commander');
program
	.option('-p, --port <port>', 'port on which to listen to (defaults to 8124)',parseInt)
	.option('-r, --remote <host:port>', 'remote host to pipe to')
	.parse(process.argv);

var port = program.port || 8124;
var remote = program.remote.toString().split(':');
if (remote.length != 2 || parseInt(remote[1]) <= 0) {
	console.log('please specify a remote host to pipe to');
	program.exit();
}

console.log('listening on port ' + port);
console.log('piping to remote ' + remote[0] + ':' + parseInt(remote[1]));

var net = require('net');
var server = net.createServer(function(c) {
	console.log('client connection');
	var rm = new net.Socket();
	rm.on('error', function (e) {
		console.log('error connecting to remote. ' + e.toString());
	});
	rm.connect(parseInt(remote[1]), remote[0],
		function () {
			console.log('connected to ' + rm.remoteAddress
				+ ' for remote address = ' + c.remoteAddress);
			rm.removeAllListeners('error');
			rm.on('error', function (e) {
				console.log('error occured on outgoing end of pipe. Info = ' + e.toString());
				rm.unpipe(c);
				c.unpipe(rm);
				c.end();
			});
			c.pipe(rm);
			rm.pipe(c);
		});
	c.on('end', function() {
		console.log('client disconnected');
	});
	c.on('error', function (e) {
		console.log('error occured on incoming end of pipe. Info = ' + e.toString());
	});
	c.on('timeout', function (e) {
		console.log('timeout trying to connect to remote. ' + e.toString());
	});
});

server.on('error', function (e) {
	console.log('server error - ' + e.toString());
});

server.listen(port, function() {
	console.log('server bound');
});
