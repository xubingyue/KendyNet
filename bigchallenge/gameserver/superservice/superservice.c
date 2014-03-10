#include "common/cmd.h"
#include "avatar.h"
#include "battleservice/battleservice.h"
#include "core/lua_util.h"
#include "superservice.h"

superservice_t g_superservice = NULL;
static cmd_handler_t super_cmd_handlers[MAX_CMD] = {NULL};

ident* players[MAX_PLAYER] = {NULL}; 

int32_t send2gate(wpacket_t wpk)
{
	if(!g_superservice || !wpk) return -1;
	if(!is_vaild_ident(TO_IDENT(g_superservice->togate))) return -1;
	return asyn_send(g_superservice->togate,wpk);
}

void super_connect(msgdisp_t disp,sock_ident sock,const char *ip,int32_t port)
{
    disp->bind(disp,0,sock,65536,1,3*1000,0);//由系统选择poller
}

void super_connected(msgdisp_t disp,sock_ident sock,const char *ip,int32_t port)
{
	g_superservice->togate = sock;
}

void super_disconnected(msgdisp_t disp,sock_ident sock,const char *ip,int32_t port,uint32_t err)
{
	if(EQ_IDENT(g_superservice->togate,sock))
		MAKE_EMPTY_IDENT(g_superservice->togate);
}


int32_t super_processpacket(msgdisp_t disp,rpacket_t rpk)
{
	/*uint16_t cmd = rpk_peek_uint16(rpk);
	if(cmd >= CMD_CLIENT2BATTLE && cmd <= CMD_CLIENT2BATTLE_END)
	{
		//将消息转发到battle
		avatarid _avatid = rpk_reverse_read_avatarid(rpk);
		battleservice_t battle = get_battle_by_index((uint8_t)_avatid.battleservice_id);
		if(battle && 0 == push_msg(battle->msgdisp,rpk))
			return 0;//不销毁rpk,由battleservice负责销毁
	}else{
		if(super_cmd_handlers[cmd]){
			rpk_read_uint16(rpk);//丢弃cmd
			call_handler(super_cmd_handlers[cmd],rpk);
		}else{
			//记录日志
		}
	}*/
    return 1;
}

void reg_super_cmd_handler(uint16_t cmd,cmd_handler_t handler)
{
	if(cmd < MAX_CMD) super_cmd_handlers[cmd] = handler;
}

player_t cast2player(avatarid _avatarid)
{
	uint32_t index = _avatarid.ojb_index;
	if(index >= MAX_PLAYER) return NULL;
	ident *_ident = players[index];
	if(_ident){
		return (player_t)cast_2_refbase(*_ident);
	}
	return NULL;
}

void start_superservice()
{
	g_superservice = calloc(1,sizeof(*g_superservice));
}
