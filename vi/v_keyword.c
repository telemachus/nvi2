/*-
 * Copyright (c) 2026
 *	Peter Aronoff.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "vi.h"

/*
 * v_keyword --
 *	Display man page for the keyword under cursor.
 *
 * PUBLIC: int v_keyword(SCR *, VICMD *);
 */
int
v_keyword(SCR *sp, VICMD *vp)
{
	size_t blen, len;
	int ifcontinue = 0;
	CHAR_T *bp;

	/* Build "!man keyword" command string. */
	len = SIZE(L("!man ")) - 1 + VIP(sp)->klen;
	GET_SPACE_RETW(sp, bp, blen, len);
	MEMCPY(bp, L("!man "), SIZE(L("!man ")) - 1);
	MEMCPY(bp + SIZE(L("!man ")) - 1, VIP(sp)->keyw, VIP(sp)->klen);

	/* Push command onto the ex command stack. */
	if (ex_run_str(sp, NULL, bp, len, 0, 0)) {
		FREE_SPACEW(sp, bp, blen);
		return (1);
	}
	FREE_SPACEW(sp, bp, blen);

	/* Home the cursor. */
	vs_home(sp);

	/* Execute the ex command. */
	(void)ex_cmd(sp);

	/* Flush ex messages. */
	(void)ex_fflush(sp);

	/*
	 * Skip the "Press any key to continue" prompt.
	 * The user already interacted with the pager (pressing 'q' to exit),
	 * so no additional confirmation is needed.
	 */
	F_SET(sp, SC_EX_WAIT_NO);

	/* Resolve any messages/screen state. */
	if (vs_ex_resolve(sp, &ifcontinue))
		return (1);

	/*
	 * Cleanup from the ex command (from v_ex_done pattern).
	 * The cursor may have changed; validate and fix it.
	 */
	if (db_eget(sp, sp->lno, NULL, &len, NULL)) {
		sp->lno = 1;
		sp->cno = 0;
	} else if (sp->cno >= len)
		sp->cno = len ? len - 1 : 0;

	vp->m_final.lno = sp->lno;
	vp->m_final.cno = sp->cno;

	/* Don't re-adjust cursor after ex command. */
	F_CLR(vp, VM_RCM_MASK);
	F_SET(vp, VM_RCM_SET);

	/* Force a full screen redraw after shell command. */
	F_SET(sp, SC_SCR_REDRAW);

	return (0);
}
