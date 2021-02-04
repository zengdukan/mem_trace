const http = require('http');
const dgram = require('dgram');

const mem_map = new Map(); // mem_addr => MemData

const svr = http.createServer();
svr.on('request', (request, response) => {
    response.writeHead(200, { 'content-type': 'application/json'});
    response.end(JSON.stringify([...mem_map.values()]));
});

svr.listen(3000, ()=> {
    console.log(`start`);
});

class MemData {
    constructor() {
        this.timestamp = '';
        this.mem_addr = '';
        this.mem_size = '',
        this.bt_size = 0;
        this.backtrace = '';
    }

    toString() {
        if (this.bt_size == 0) {
            return (`[${this.timestamp}] free 0x${this.mem_addr}, caller=0x${this.mem_size.toString(16)}`);
        } else {
            return (`[${this.timestamp}] malloc(${this.mem_size}) 0x${this.mem_addr}\n${this.backtrace}`);
        }
    }
}

const mem_recver = dgram.createSocket('udp4');
mem_recver.on('message', (msg, rinfo) => {

    const mem = new MemData();
    const timestamp = msg.readBigUInt64LE(0)/1000n;
    mem.timestamp = new Date(parseInt(timestamp)).toISOString();
    mem.mem_addr = `0x${msg.readBigUInt64LE(8).toString(16)}`;
    mem.bt_size = parseInt(msg.readBigUInt64LE(24));
    mem.mem_size = mem.bt_size == 0 ? `0x${msg.readBigUInt64LE(16).toString(16)}` : msg.readBigUInt64LE(16).toString();
    mem.backtrace = msg.toString('ascii', 32, msg.length);
    // console.log(mem.toString());
    if (mem.bt_size == 0)
        mem_map.delete(mem.mem_addr);
    else
        mem_map.set(mem.mem_addr, mem);
});


mem_recver.bind(9000);