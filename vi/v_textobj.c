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
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "vi.h"

/*
 * Text objects implement Vim's a/i object selection commands.
 * The 'a' (around) variants include delimiters/whitespace.
 * The 'i' (inner) variants exclude delimiters/whitespace.
 *
 * Supported objects:
 *   w/W - word/WORD
 *   "/'/` - quoted strings (single line)
 *   (/), b - parentheses
 *   [/] - brackets
 *   {/}, B - braces
 *   </> - angle brackets
 */

static int textobj_word(SCR *, VICMD *, int, int);
static int textobj_quote(SCR *, VICMD *, int, CHAR_T);
static int textobj_pair(SCR *, VICMD *, int, CHAR_T, CHAR_T);
static int textobj_line(SCR *, VICMD *, int);
static int textobj_entire(SCR *, VICMD *, int);

/*
 * v_textobj --
 *	Handle text object motions (iw, aw, i", a", i(, a(, etc.)
 *
 * PUBLIC: int v_textobj(SCR *, VICMD *);
 */
int
v_textobj(SCR *sp, VICMD *vp)
{
	int around;
	CHAR_T obj;

	around = (vp->key == 'a');
	obj = vp->character;

	switch (obj) {
	case 'w':
		return (textobj_word(sp, vp, around, 0));
	case 'W':
		return (textobj_word(sp, vp, around, 1));
	case '"':
	case '\'':
	case '`':
		return (textobj_quote(sp, vp, around, obj));
	case '(':
	case ')':
	case 'b':
		return (textobj_pair(sp, vp, around, '(', ')'));
	case '[':
	case ']':
		return (textobj_pair(sp, vp, around, '[', ']'));
	case '{':
	case '}':
	case 'B':
		return (textobj_pair(sp, vp, around, '{', '}'));
	case '<':
	case '>':
		return (textobj_pair(sp, vp, around, '<', '>'));
	case 'l':
		return (textobj_line(sp, vp, around));
	case 'e':
		return (textobj_entire(sp, vp, around));
	default:
		msgq(sp, M_ERR, "Unknown text object: %c", obj);
		return (1);
	}
}

/*
 * textobj_word --
 *	Select a word text object.
 *	'around' includes surrounding whitespace.
 *	'bigword' uses WORD (whitespace-delimited) instead of word.
 */
static int
textobj_word(SCR *sp, VICMD *vp, int around, int bigword)
{
	VCS cs;
	size_t len;
	CHAR_T *p;
	int in_blank;
	recno_t start_lno = 0, stop_lno = 0;
	size_t start_cno = 0, stop_cno = 0;

	/* Get current line. */
	if (db_get(sp, vp->m_start.lno, DBG_FATAL, &p, &len))
		return (1);

	if (len == 0) {
		msgq(sp, M_BERR, "Empty line");
		return (1);
	}

	cs.cs_lno = vp->m_start.lno;
	cs.cs_cno = vp->m_start.cno;
	if (cs_init(sp, &cs))
		return (1);

	/* Determine if we're in whitespace. */
	in_blank = (cs.cs_flags == 0 && ISBLANK(cs.cs_ch));

	/* Find the start of the word/blank region. */
	start_lno = cs.cs_lno;
	start_cno = cs.cs_cno;

	if (in_blank) {
		/* In whitespace: find start of whitespace region. */
		while (cs.cs_cno > 0) {
			if (cs_prev(sp, &cs))
				return (1);
			if (cs.cs_flags != 0)
				break;
			if (!ISBLANK(cs.cs_ch)) {
				if (cs_next(sp, &cs))
					return (1);
				break;
			}
			start_cno = cs.cs_cno;
		}
		if (cs.cs_cno == 0 && cs.cs_flags == 0 && ISBLANK(cs.cs_ch))
			start_cno = 0;
	} else {
		/* In a word: find start of word. */
		while (cs.cs_cno > 0) {
			if (cs_prev(sp, &cs))
				return (1);
			if (cs.cs_flags != 0)
				break;
			if (bigword) {
				if (ISBLANK(cs.cs_ch)) {
					if (cs_next(sp, &cs))
						return (1);
					break;
				}
			} else {
				if (!inword(cs.cs_ch)) {
					if (cs_next(sp, &cs))
						return (1);
					break;
				}
			}
			start_cno = cs.cs_cno;
		}
		if (cs.cs_cno == 0 && cs.cs_flags == 0) {
			if (bigword) {
				if (!ISBLANK(cs.cs_ch))
					start_cno = 0;
			} else {
				if (inword(cs.cs_ch))
					start_cno = 0;
			}
		}
	}

	/* Reset to starting position to find the end. */
	cs.cs_lno = vp->m_start.lno;
	cs.cs_cno = vp->m_start.cno;
	if (cs_init(sp, &cs))
		return (1);

	stop_lno = cs.cs_lno;
	stop_cno = cs.cs_cno;

	if (in_blank) {
		/* In whitespace: find end of whitespace region. */
		while (cs.cs_cno + 1 < len) {
			if (cs_next(sp, &cs))
				return (1);
			if (cs.cs_flags != 0)
				break;
			if (!ISBLANK(cs.cs_ch))
				break;
			stop_cno = cs.cs_cno;
		}
	} else {
		/* In a word: find end of word. */
		while (cs.cs_cno < len - 1) {
			if (cs_next(sp, &cs))
				return (1);
			if (cs.cs_flags != 0)
				break;
			if (bigword) {
				if (ISBLANK(cs.cs_ch))
					break;
			} else {
				if (!inword(cs.cs_ch))
					break;
			}
			stop_cno = cs.cs_cno;
		}
	}

	/* For 'around' variant, include trailing or leading whitespace. */
	if (around && !in_blank) {
		/* Try to include trailing whitespace first. */
		cs.cs_lno = stop_lno;
		cs.cs_cno = stop_cno;
		if (cs_init(sp, &cs))
			return (1);

		if (cs.cs_cno < len - 1) {
			if (cs_next(sp, &cs))
				return (1);
			if (cs.cs_flags == 0 && ISBLANK(cs.cs_ch)) {
				/* Include trailing whitespace. */
				stop_cno = cs.cs_cno;
				while (cs.cs_cno < len - 1) {
					if (cs_next(sp, &cs))
						return (1);
					if (cs.cs_flags != 0)
						break;
					if (!ISBLANK(cs.cs_ch))
						break;
					stop_cno = cs.cs_cno;
				}
			}
		} else if (start_cno > 0) {
			/* No trailing whitespace, try leading. */
			cs.cs_lno = start_lno;
			cs.cs_cno = start_cno;
			if (cs_init(sp, &cs))
				return (1);

			if (cs_prev(sp, &cs))
				return (1);
			if (cs.cs_flags == 0 && ISBLANK(cs.cs_ch)) {
				start_cno = cs.cs_cno;
				while (cs.cs_cno > 0) {
					if (cs_prev(sp, &cs))
						return (1);
					if (cs.cs_flags != 0)
						break;
					if (!ISBLANK(cs.cs_ch)) {
						if (cs_next(sp, &cs))
							return (1);
						break;
					}
					start_cno = cs.cs_cno;
				}
				if (cs.cs_cno == 0 && cs.cs_flags == 0 &&
				    ISBLANK(cs.cs_ch))
					start_cno = 0;
			}
		}
	}

	/* Set the range. */
	vp->m_start.lno = start_lno;
	vp->m_start.cno = start_cno;
	vp->m_stop.lno = stop_lno;
	vp->m_stop.cno = stop_cno;
	vp->m_final = vp->m_start;

	return (0);
}

/*
 * textobj_quote --
 *	Select a quoted string text object (single line only).
 */
static int
textobj_quote(SCR *sp, VICMD *vp, int around, CHAR_T quote)
{
	size_t len, cno, i;
	CHAR_T *p;
	size_t start_cno, stop_cno;
	int found_start, found_stop;
	int in_quotes;

	if (db_get(sp, vp->m_start.lno, DBG_FATAL, &p, &len))
		return (1);

	if (len == 0) {
		msgq(sp, M_BERR, "Empty line");
		return (1);
	}

	cno = vp->m_start.cno;
	start_cno = 0;
	stop_cno = 0;
	found_start = 0;
	found_stop = 0;
	in_quotes = 0;

	/*
	 * Scan from start of line to find the quoted region containing
	 * or following the cursor.
	 */
	for (i = 0; i < len; i++) {
		if (p[i] == quote) {
			if (!in_quotes) {
				/* Opening quote. */
				start_cno = i;
				in_quotes = 1;
			} else {
				/* Closing quote. */
				stop_cno = i;
				in_quotes = 0;

				/* Check if cursor is within this quoted region. */
				if (cno >= start_cno && cno <= stop_cno) {
					found_start = 1;
					found_stop = 1;
					break;
				}
				/* If cursor is before this region, use it. */
				if (cno < start_cno) {
					found_start = 1;
					found_stop = 1;
					break;
				}
			}
		}
	}

	if (!found_start || !found_stop) {
		msgq(sp, M_BERR, "No quoted string found");
		return (1);
	}

	if (around) {
		/* Include the quotes. */
		vp->m_start.cno = start_cno;
		vp->m_stop.cno = stop_cno;
	} else {
		/* Exclude the quotes. */
		if (stop_cno <= start_cno + 1) {
			msgq(sp, M_BERR, "Empty quoted string");
			return (1);
		}
		vp->m_start.cno = start_cno + 1;
		vp->m_stop.cno = stop_cno - 1;
	}

	vp->m_stop.lno = vp->m_start.lno;
	vp->m_final = vp->m_start;

	return (0);
}

/*
 * textobj_pair --
 *	Select a paired delimiter text object (can span multiple lines).
 */
static int
textobj_pair(SCR *sp, VICMD *vp, int around, CHAR_T open, CHAR_T close)
{
	VCS cs;
	int cnt;
	recno_t start_lno = 0, stop_lno = 0;
	size_t start_cno = 0, stop_cno = 0;
	int found_open, found_close;

	cs.cs_lno = vp->m_start.lno;
	cs.cs_cno = vp->m_start.cno;
	if (cs_init(sp, &cs))
		return (1);

	/*
	 * If cursor is on the open delimiter, search forward for close.
	 * If cursor is on the close delimiter, search backward for open.
	 * Otherwise, first search backward for the enclosing open delimiter.
	 */
	found_open = 0;
	found_close = 0;

	/* Check if we're on a delimiter. */
	if (cs.cs_flags == 0 && cs.cs_ch == open) {
		start_lno = cs.cs_lno;
		start_cno = cs.cs_cno;
		found_open = 1;
	} else if (cs.cs_flags == 0 && cs.cs_ch == close) {
		stop_lno = cs.cs_lno;
		stop_cno = cs.cs_cno;
		found_close = 1;
	}

	/* Search backward for opening delimiter. */
	if (!found_open) {
		cnt = 0;
		for (;;) {
			if (cs_prev(sp, &cs))
				return (1);
			if (cs.cs_flags == CS_SOF) {
				msgq(sp, M_BERR, "No matching '%c' found", open);
				return (1);
			}
			if (cs.cs_flags != 0)
				continue;
			if (cs.cs_ch == close)
				++cnt;
			else if (cs.cs_ch == open) {
				if (cnt == 0) {
					start_lno = cs.cs_lno;
					start_cno = cs.cs_cno;
					found_open = 1;
					break;
				}
				--cnt;
			}
		}
	}

	/* Search forward for closing delimiter. */
	if (!found_close) {
		/* Reset to starting position. */
		cs.cs_lno = vp->m_start.lno;
		cs.cs_cno = vp->m_start.cno;
		if (cs_init(sp, &cs))
			return (1);

		cnt = 0;
		for (;;) {
			if (cs_next(sp, &cs))
				return (1);
			if (cs.cs_flags == CS_EOF) {
				msgq(sp, M_BERR, "No matching '%c' found", close);
				return (1);
			}
			if (cs.cs_flags != 0)
				continue;
			if (cs.cs_ch == open)
				++cnt;
			else if (cs.cs_ch == close) {
				if (cnt == 0) {
					stop_lno = cs.cs_lno;
					stop_cno = cs.cs_cno;
					found_close = 1;
					break;
				}
				--cnt;
			}
		}
	}

	if (!found_open || !found_close) {
		msgq(sp, M_BERR, "No matching pair found");
		return (1);
	}

	if (around) {
		/* Include the delimiters. */
		vp->m_start.lno = start_lno;
		vp->m_start.cno = start_cno;
		vp->m_stop.lno = stop_lno;
		vp->m_stop.cno = stop_cno;
	} else {
		/* Exclude the delimiters. */
		vp->m_start.lno = start_lno;
		vp->m_start.cno = start_cno + 1;
		vp->m_stop.lno = stop_lno;
		if (stop_cno == 0) {
			msgq(sp, M_BERR, "Empty pair");
			return (1);
		}
		vp->m_stop.cno = stop_cno - 1;

		/* Handle case where delimiters are adjacent. */
		if (vp->m_start.lno == vp->m_stop.lno &&
		    vp->m_start.cno > vp->m_stop.cno) {
			msgq(sp, M_BERR, "Empty pair");
			return (1);
		}

		/* Adjust if start moved to next line. */
		if (vp->m_start.lno == start_lno) {
			size_t slen;
			if (db_get(sp, start_lno, 0, NULL, &slen) == 0) {
				if (vp->m_start.cno >= slen) {
					/* Move to start of next line. */
					vp->m_start.lno++;
					vp->m_start.cno = 0;
				}
			}
		}
	}

	vp->m_final = vp->m_start;

	return (0);
}

/*
 * textobj_line --
 *	Select a line text object.
 *	'around' (al): entire line from column 0 to last character (not newline).
 *	'inner' (il): from first non-whitespace to last non-whitespace.
 */
static int
textobj_line(SCR *sp, VICMD *vp, int around)
{
	size_t len;
	CHAR_T *p;
	size_t start_cno, stop_cno;

	if (db_get(sp, vp->m_start.lno, DBG_FATAL, &p, &len))
		return (1);

	if (len == 0) {
		msgq(sp, M_BERR, "Empty line");
		return (1);
	}

	if (around) {
		/* al: entire line content. */
		start_cno = 0;
		stop_cno = len - 1;
	} else {
		/* il: first non-whitespace to last non-whitespace. */
		start_cno = 0;
		while (start_cno < len && ISBLANK(p[start_cno]))
			start_cno++;

		if (start_cno >= len) {
			msgq(sp, M_BERR, "Line contains only whitespace");
			return (1);
		}

		stop_cno = len - 1;
		while (stop_cno > start_cno && ISBLANK(p[stop_cno]))
			stop_cno--;
	}

	vp->m_start.cno = start_cno;
	vp->m_stop.lno = vp->m_start.lno;
	vp->m_stop.cno = stop_cno;
	vp->m_final = vp->m_start;

	return (0);
}

/*
 * textobj_entire --
 *	Select the entire buffer as a text object.
 *	'around' (ae): all lines in the buffer (line-mode).
 *	'inner' (ie): from first non-ws char in first non-blank line
 *	              to last non-ws char in last non-blank line (char-mode).
 */
static int
textobj_entire(SCR *sp, VICMD *vp, int around)
{
	recno_t lno, last_lno;
	recno_t start_lno, stop_lno;
	size_t len;
	size_t start_cno, stop_cno;
	CHAR_T *p;

	start_lno = stop_lno = 0;
	start_cno = stop_cno = 0;

	/* Get the last line number. */
	if (db_last(sp, &last_lno))
		return (1);

	if (last_lno == 0) {
		msgq(sp, M_BERR, "Empty buffer");
		return (1);
	}

	if (around) {
		/* ae: line-mode, entire buffer. */
		vp->m_start.lno = 1;
		vp->m_start.cno = 0;
		vp->m_stop.lno = last_lno;
		if (db_get(sp, last_lno, DBG_FATAL, NULL, &len))
			return (1);
		vp->m_stop.cno = len ? len - 1 : 0;
		vp->m_final = vp->m_start;
		F_SET(vp, VM_LMODE);
	} else {
		/* ie: character-mode, first non-ws to last non-ws. */

		/* Find first line with non-whitespace content. */
		for (lno = 1; lno <= last_lno; lno++) {
			if (db_get(sp, lno, DBG_FATAL, &p, &len))
				return (1);
			if (len == 0)
				continue;
			/* Check if line has non-whitespace. */
			for (start_cno = 0; start_cno < len; start_cno++) {
				if (!ISBLANK(p[start_cno]))
					break;
			}
			if (start_cno < len) {
				start_lno = lno;
				break;
			}
		}

		if (lno > last_lno) {
			msgq(sp, M_BERR, "Buffer contains only whitespace");
			return (1);
		}

		/* Find last line with non-whitespace content. */
		for (lno = last_lno; lno >= 1; lno--) {
			if (db_get(sp, lno, DBG_FATAL, &p, &len))
				return (1);
			if (len == 0)
				continue;
			/* Check if line has non-whitespace. */
			stop_cno = len - 1;
			while (stop_cno > 0 && ISBLANK(p[stop_cno]))
				stop_cno--;
			if (!ISBLANK(p[stop_cno])) {
				stop_lno = lno;
				break;
			}
		}

		vp->m_start.lno = start_lno;
		vp->m_start.cno = start_cno;
		vp->m_stop.lno = stop_lno;
		vp->m_stop.cno = stop_cno;
		vp->m_final = vp->m_start;
	}

	return (0);
}
