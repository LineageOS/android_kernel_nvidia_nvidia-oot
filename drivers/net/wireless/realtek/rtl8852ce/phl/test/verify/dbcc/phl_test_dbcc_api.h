/******************************************************************************
 *
 * Copyright(c) 2019 Realtek Corporation.
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
#ifndef _PHL_TEST_DBCC_API_H_
#define _PHL_TEST_DBCC_API_H_

#ifdef CONFIG_PHL_TEST_VERIFY
#ifdef CONFIG_DBCC_SUPPORT
enum rtw_phl_status rtw_test_dbcc_cmd_process(void *priv);
#endif /* CONFIG_DBCC_SUPPORT */
#endif /* CONFIG_PHL_TEST_VERIFY */
#endif /* _PHL_TEST_DBCC_API_H_ */
