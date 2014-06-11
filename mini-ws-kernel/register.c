#include "config.h"
#include "epan/epan_dissect.h"
#include "epan/epan.h"
#include "register.h"


void register_all_protocols(register_cb cb, gpointer client_data)
{

	{extern void proto_register_ber (void); if(cb) (*cb)(RA_REGISTER, "proto_register_ber", client_data); proto_register_ber ();}
	{extern void proto_register_data (void); if(cb) (*cb)(RA_REGISTER, "proto_register_data", client_data); proto_register_data ();}
	{extern void proto_register_frame (void); if(cb) (*cb)(RA_REGISTER, "proto_register_frame", client_data); proto_register_frame ();}
	{extern void proto_register_per (void); if(cb) (*cb)(RA_REGISTER, "proto_register_per", client_data); proto_register_per ();}
	{extern void proto_register_ethertype (void); if(cb) (*cb)(RA_REGISTER, "proto_register_ethertype", client_data); proto_register_ethertype ();}
	{extern void proto_register_eth (void); if(cb) (*cb)(RA_REGISTER, "proto_register_eth", client_data); proto_register_eth ();}    	
 
	  {extern void proto_register_etherip (void); if(cb) (*cb)(RA_REGISTER, "proto_register_etherip", client_data); proto_register_etherip ();}	

	 {extern void proto_register_e212 (void); if(cb) (*cb)(RA_REGISTER, "proto_register_e212", client_data); proto_register_e212 ();}
  {extern void proto_register_ip (void); if(cb) (*cb)(RA_REGISTER, "proto_register_ip", client_data); proto_register_ip ();}
  {extern void proto_register_sctp (void); if(cb) (*cb)(RA_REGISTER, "proto_register_sctp", client_data); proto_register_sctp ();}
  {extern void proto_register_s1ap (void); if(cb) (*cb)(RA_REGISTER, "proto_register_s1ap", client_data); proto_register_s1ap ();}

}

void register_all_protocol_handoffs(register_cb cb, gpointer client_data)
{

	{extern void proto_reg_handoff_ber (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_ber", client_data); proto_reg_handoff_ber ();}
	{extern void proto_reg_handoff_data (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_data", client_data); proto_reg_handoff_data ();}
	{extern void proto_reg_handoff_frame (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_frame", client_data); proto_reg_handoff_frame ();}
	{extern void proto_reg_handoff_per (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_per", client_data); proto_reg_handoff_per ();}
	{extern void proto_reg_handoff_ethertype (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_ethertype", client_data); proto_reg_handoff_ethertype ();}
	{extern void proto_reg_handoff_eth (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_eth", client_data); proto_reg_handoff_eth ();}    	
  
	{extern void proto_reg_handoff_etherip (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_etherip", client_data); proto_reg_handoff_etherip ();}
  
  {extern void proto_reg_handoff_ip (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_ip", client_data); proto_reg_handoff_ip ();}
  {extern void proto_reg_handoff_s1ap (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_s1ap", client_data); proto_reg_handoff_s1ap ();}
  {extern void proto_reg_handoff_sctp (void); if(cb) (*cb)(RA_HANDOFF, "proto_reg_handoff_sctp", client_data); proto_reg_handoff_sctp ();}


}

void splash_update(register_action_e action, const char *message, gpointer client_data)
{
}

void printError(const char * a, va_list ap)
{
}

void printOpenError(const char * a, int b, gboolean c)
{
}

void printReadError(const char * a, int b)
{
}

void printWriteError(const char * a, int b)
{
}

extern void init_dissection(void);
extern void cleanup_dissection(void);

/* __declspec(dllexport)*/ int InitWireshark()
{
	static int bFirst = 1;
	if (bFirst)
	{
		epan_init(register_all_protocols,register_all_protocol_handoffs,
			splash_update,NULL,printError, printOpenError, printReadError, printWriteError);

		bFirst = 0;
	}

	init_dissection();

	return 0;
}

/*__declspec(dllexport)*/ int DeInitWireshark()
{
	cleanup_dissection();

	return 0;
}

//  __declspec(dllexport) extern void epan_dissect_free(epan_dissect_t* edt);
//  __declspec(dllexport) extern void epan_dissect_run(epan_dissect_t *edt, void* pseudo_header,const guint8* data, frame_data *fd, column_info *cinfo);
//  __declspec(dllexport) extern epan_dissect_t* epan_dissect_new(const gboolean create_proto_tree, const gboolean proto_tree_visible);
