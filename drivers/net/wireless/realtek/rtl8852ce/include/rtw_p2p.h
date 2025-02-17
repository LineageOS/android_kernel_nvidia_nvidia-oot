/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __RTW_P2P_H_
#define __RTW_P2P_H_


u32 build_prov_disc_request_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8 *pssid, u8 ussidlen, u8 *pdev_raddr);
#ifdef CONFIG_WFD
int rtw_init_wifi_display_info(_adapter *padapter);
void rtw_wfd_enable(_adapter *adapter, bool on);
void rtw_wfd_set_ctrl_port(_adapter *adapter, u16 port);
void rtw_tdls_wfd_enable(_adapter *adapter, bool on);

u32 build_probe_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_probe_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8 tunneled);
u32 build_beacon_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_nego_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_nego_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_nego_confirm_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_invitation_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_invitation_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_assoc_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_assoc_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_provdisc_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_provdisc_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);

u32 rtw_append_beacon_wfd_ie(_adapter *adapter, u8 *pbuf);
u32 rtw_append_probe_req_wfd_ie(_adapter *adapter, u8 *pbuf);
u32 rtw_append_probe_resp_wfd_ie(_adapter *adapter, u8 *pbuf);
u32 rtw_append_assoc_req_wfd_ie(_adapter *adapter, u8 *pbuf);
u32 rtw_append_assoc_resp_wfd_ie(_adapter *adapter, u8 *pbuf);
#endif /*CONFIG_WFD */

void rtw_xframe_chk_wfd_ie(struct xmit_frame *xframe);

u32 process_probe_req_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u32 process_assoc_req_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pframe, uint len, struct sta_info *psta);
u8 process_p2p_presence_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
int process_p2p_cross_connect_ie(_adapter *padapter, u8 *IEs, u32 IELength);

#ifdef CONFIG_P2P_PS
void	process_p2p_ps_ie(_adapter *padapter, u8 *IEs, u32 IELength);
void	p2p_ps_wk_hdl(_adapter *padapter, u8 p2p_ps_state);
u8	p2p_ps_wk_cmd(_adapter *padapter, u8 p2p_ps_state, u8 enqueue);
u8 *rtw_append_p2p_go_noa_ie(struct _ADAPTER *adapter, u8 *frame, u32 *len);
void rtw_core_register_p2pps_ops(struct dvobj_priv *dvobj);
void rtw_append_probe_resp_p2p_go_noa(struct xmit_frame *xframe);
#endif /* CONFIG_P2P_PS */
#ifdef CONFIG_P2P
void rtw_append_probe_resp_p2p_ie(struct xmit_frame *xframe);
void rtw_append_probe_resp_vendor_ie(struct xmit_frame *xframe);
#endif
#ifdef CONFIG_IOCTL_CFG80211
int rtw_p2p_check_frames(_adapter *padapter, const u8 *buf, u32 len, u8 tx);
#endif /* CONFIG_IOCTL_CFG80211 */

void reset_global_wifidirect_info(_adapter *padapter);
void init_wifidirect_info(_adapter *padapter, enum P2P_ROLE role);
int rtw_p2p_enable(_adapter *padapter, enum P2P_ROLE role);

void _rtw_p2p_set_role(struct wifidirect_info *wdinfo, enum P2P_ROLE role);

static inline int _rtw_p2p_role(struct wifidirect_info *wdinfo)
{
	return wdinfo->role;
}
static inline bool _rtw_p2p_chk_role(struct wifidirect_info *wdinfo, enum P2P_ROLE role)
{
	return wdinfo->role == role;
}

#ifdef CONFIG_DBG_P2P
void dbg_rtw_p2p_set_role(struct wifidirect_info *wdinfo, enum P2P_ROLE role, const char *caller, int line);
#define rtw_p2p_set_role(wdinfo, role) dbg_rtw_p2p_set_role(wdinfo, role, __FUNCTION__, __LINE__)
#else /* CONFIG_DBG_P2P */
#define rtw_p2p_set_role(wdinfo, role) _rtw_p2p_set_role(wdinfo, role)
#endif /* CONFIG_DBG_P2P */

#define rtw_p2p_role(wdinfo) _rtw_p2p_role(wdinfo)
#define rtw_p2p_chk_role(wdinfo, role) _rtw_p2p_chk_role(wdinfo, role)

#endif
