#include "net/ef_sock.h"
#include "proto/msg_head.h"
#include "proto/peer_server.pb.h"
#include "base/ef_base64.h"
#include "base/ef_hex.h"
#include "base/ef_utility.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

using namespace ef;
using namespace std;
using namespace gim;


#define isBigEndian()   ((*(unsigned short *) ("KE") >> 8) == 'K')
#define rol2(x,n)( ((x) << (n)) | ((x) >> (64-(n))) )
#define swap8(b) ( (rol2((uint64_t)(b),8 )&0x000000FF000000FFULL) | \
                   (rol2((uint64_t)(b),24)&0x0000FF000000FF00ULL) | \
                   (rol2((uint64_t)(b),40)&0x00FF000000FF0000ULL) | \
                   (rol2((uint64_t)(b),56)&0xFF000000FF000000ULL) )
#ifndef htonll
#define htonll(b) ( isBigEndian()?(b):swap8(b) )
#endif
#ifndef ntohll
#define ntohll(b) ( isBigEndian()?(b):swap8(b) )
#endif

#define STATUS_OK (0)
#define STATUS_SOCKET_ERR (-1)
#define STATUS_OTHER_ERR  (-1000)

enum{
	STATUS_INIT = 0,
	STATUS_REG = 1,
};

class publish_client{
public:
	publish_client():m_status(STATUS_INIT){
	}
	
	int32 	connect_server(const char* svaddr, int svport){
                int     consuc = 0;


                m_server_addr = svaddr;
                m_server_port = svport;

                m_fd = tcpConnect(const_cast<char*>(svaddr), svport, NULL, 0);

                if(m_fd == INVALID_SOCKET || consuc < 0){
                        std::cout << "connect to:" 
                                << svaddr << ", port:" << svport << " fail!";
                        return  STATUS_SOCKET_ERR;
                }
                return  STATUS_OK;
        }

        int32   const_req(int c, std::string &req, const std::string &body){

		std::cout << "cmd:" << c << std::endl;

                int32   len = 4 + 4 + 4 + body.size();
                uint32  magic = htonl(0x20140417);
		uint32  cmd = htonl(c);
                uint32  nlen = htonl(len);

                req.append((char*)&magic, 4);
                req.append((char*)&nlen, 4);
		req.append((char*)&cmd, 4);
                req.append(body.data(), body.size());

                return  (int32)len;
        }

	int32	parse_service_req(const std::string& resp, 
			PushMessageResponse& svreq){
		return svreq.ParseFromString(resp);
	}

	int32	const_service_req(const PushMessageRequest& srreq, 
			std::string& req){
		return srreq.SerializeToString(&req);
	}

	int32	parse_service_resp(const std::string& resp, 
			PushMessageResponse& svresp){
		return svresp.ParseFromString(resp);
	}

	int32	const_service_resp(const PushMessageResponse& srresp,
			std::string& req){
		return srresp.SerializeToString(&req);
	}

        int32   disconnect(){
                if(m_fd != INVALID_SOCKET){
                        closesocket(m_fd);
                        m_fd = INVALID_SOCKET;
                }
                return  0;
        }	

	int32	push_message(const std::string& from, const std::string& to, 
		int32 type, const std::string& sn, const std::string& data){
		PushMessageRequest pshreq;
		pshreq.set_sn(sn);
		Message* m = pshreq.mutable_msg();
		m->set_from(from);
		m->set_to(to);
		m->set_type(type);
		m->set_sn(sn);
		m->set_data(data);
		std::string body;
		const_service_req(pshreq, body);
		std::cout << "body.size:" << body.size() << std::endl;
		std::string respbody;
		do_request(200, body, respbody);
		PushMessageResponse pshresp;
		parse_service_resp(respbody, pshresp);
		std::cout << "push response: sn:" << pshresp.sn()
			<< ", status:" << pshresp.status() << std::endl;
		return 0;
	}

        int32   do_recv(std::string  &msg){

                uint32  totallen = 0;
                int     ret = 0;
                int     rcvcnt = 0;


                msg.resize(12);
                char* buf = (char*)msg.data();

		while(rcvcnt < 12){
			ret = recv(m_fd, buf + rcvcnt, 12 - rcvcnt, 0);
			if(ret <= 0){
				std::cout << " server addr:" <<  m_server_addr
					<< ", port:" << m_server_port
					<< " recv head fail, errno:" << strerror(errno)
					<< std::endl;
				return  STATUS_SOCKET_ERR;
			}
			rcvcnt+= ret;
		}

                totallen = *(uint32*)(buf + 4);
                totallen = ntohl(totallen);
                rcvcnt = 12;

                msg.resize(totallen);
                buf = (char*)msg.data() + 12;

                while(rcvcnt < totallen){
                        ret = recv(m_fd, buf, totallen - rcvcnt, 0);

                        if(ret <= 0){
                                std::cout << " server addr:" << m_server_addr
                                        << ", port:" << m_server_port 
                                        << "recv body fail, err:" << strerror(errno)
					<< std::endl;
                                return  STATUS_SOCKET_ERR;
                        }
                        rcvcnt += ret;
                        buf += ret;
                }
		//std::cout << "msg.size:" << msg.size() << std::endl;
                return  totallen;

        }

        int32   do_request(int cmd, const std::string& req, std::string& resp){
                int32   ret = 0;
                if(m_fd == INVALID_SOCKET){
                        ret = connect_server(m_server_addr.data(), m_server_port);
                }
                if(ret < 0){
                        std::cout << "connect addr:" << m_server_addr
                                << ",port:" << m_server_port << " fail, err:"
				<< strerror(errno) << std::endl;
                }

                ret = send_req(cmd, req);
                if(ret < 0){
                        goto    exit;
                }

                ret = recv_resp(resp);
        exit:
                if(ret < 0){
                        disconnect();
                }else{
                        std::cout << "connect addr:" << m_server_addr
                                << ",port:" << m_server_port 
                                << " do_request success!\n";
                }

                return  ret;
        }


        int32   recv_resp(std::string &resp){

                int     ret = 0;
                uint32  totallen = 0;
                std::string str;

                ret = do_recv(str);

                if(ret < 0){
                        std::cout << " server addr:" << m_server_addr
                                << ", port:" << m_server_port 
                                << " recv resp fail\n";
                        return  ret;
                }
		
		resp = str.substr(12);

                return  ret;
        }

        int32   send_req(int cmd, const std::string& req){
                std::string msg;

                const_req(cmd, msg, req);

		int32 ret = 0;
		ret = send(m_fd, msg.data(), msg.size(), 0);
		if(ret <= 0){
			std::cout << " server addr:" << m_server_addr
				<< ", port:" << m_server_port 
				<< " send req fail, err:" 
				<< strerror(errno);
			return  ret;
		}else{
			std::cout << " server addr:" << m_server_addr
				<< ", port:" << m_server_port
				<< " send ret:" << ret << std::endl;
		}

                return  ret;
        }	
private:
	std::string	m_server_addr;
	int		m_server_port;
	std::string	m_buf;
	int32		m_status;
	SOCKET	m_fd;
}; 



int main(int argc, const char** argv){
	if(argc < 3){
		std::cout << "test <addr> <port>\n push <from> <to> <type> <sn> <data>"
			<< std::endl;
		return	0;
	}
	int	ret = 0;
	publish_client cli;
	ret = cli.connect_server(argv[1], atoi(argv[2]));
	if(ret < 0){
		std::cout << "connect server fail";
		return	ret;
	}
	std::string line;
	std::vector<std::string> pars;
	while(getline(cin, line)){
		ef::split(line, pars, " ");
		if(pars.size() <= 0){
			continue;
		}
		if(pars[0] == "push"){
			cli.push_message(pars[1], pars[2], 
			atoi(pars[3].data()), pars[4], pars[5]);
		}	
	}

	return	0;
}
