/*
 *      highlighting.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2005-2008 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
 *      Copyright 2006-2008 Nick Treleaven <nick(dot)treleaven(at)btinternet(dot)com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $Id$
 */

/*
 * Syntax highlighting for the different filetypes, using the Scintilla lexers.
 */

#include <stdlib.h>

#include "SciLexer.h"
#include "geany.h"
#include "highlighting.h"
#include "editor.h"
#include "utils.h"
#include "filetypes.h"
#include "symbols.h"


/* Whitespace has to be set after setting wordchars. */
#define GEANY_WHITESPACE_CHARS " \t" "!\"#$%&'()*+,-./:;<=>?@[\\]^`{|}~"

static gchar *whitespace_chars;

static void styleset_markup(ScintillaObject *sci, gboolean set_keywords);


typedef struct
{
	HighlightingStyle	 *styling;		/* array of styles, NULL if not used or uninitialised */
	gchar				**keywords;
	gchar				 *wordchars;	/* NULL used for style sets with no styles */
} StyleSet;

/* each filetype has a styleset except GEANY_FILETYPE_ALL */
static StyleSet style_sets[GEANY_MAX_BUILT_IN_FILETYPES - 1] = {{NULL, NULL, NULL}};


enum	/* Geany common styling */
{
	GCS_DEFAULT,
	GCS_SELECTION,
	GCS_BRACE_GOOD,
	GCS_BRACE_BAD,
	GCS_MARGIN_LINENUMBER,
	GCS_MARGIN_FOLDING,
	GCS_CURRENT_LINE,
	GCS_CARET,
	GCS_INDENT_GUIDE,
	GCS_WHITE_SPACE,
	GCS_LINE_WRAP_VISUALS,
	GCS_LINE_WRAP_INDENT,
	GCS_TRANSLUCENCY,
	GCS_MARKER_LINE,
	GCS_MARKER_SEARCH,
	GCS_MARKER_TRANSLUCENCY,
	GCS_MAX
};

typedef struct
{
	/* can take values 1 or 2 (or 3) */
	guint marker:2;
	guint lines:2;
	guint draw_line:3;
} FoldingStyle;

static struct
{
	HighlightingStyle	 styling[GCS_MAX];
	FoldingStyle		 folding_style;
	gboolean			 invert_all;
	gchar				*wordchars;
} common_style_set;


/* used for default styles */
typedef struct
{
	gchar				*name;
	HighlightingStyle	*style;
} StyleEntry;


static void new_style_array(gint file_type_id, gint styling_count)
{
	style_sets[file_type_id].styling = g_new0(HighlightingStyle, styling_count);
}


static void get_keyfile_keywords(GKeyFile *config, GKeyFile *configh, const gchar *section,
				const gchar *key, gint index, gint pos, const gchar *default_value)
{
	gchar *result;

	if (config == NULL || configh == NULL || section == NULL)
	{
		style_sets[index].keywords[pos] = g_strdup(default_value);
		return;
	}

	result = g_key_file_get_string(configh, section, key, NULL);
	if (result == NULL) result = g_key_file_get_string(config, section, key, NULL);

	if (result == NULL)
	{
		style_sets[index].keywords[pos] = g_strdup(default_value);
	}
	else
	{
		style_sets[index].keywords[pos] = result;
	}
}


static void get_keyfile_wordchars(GKeyFile *config, GKeyFile *configh, gchar **wordchars)
{
	gchar *result;

	if (config == NULL || configh == NULL)
	{
		*wordchars = g_strdup(GEANY_WORDCHARS);
		return;
	}

	result = g_key_file_get_string(configh, "settings", "wordchars", NULL);
	if (result == NULL) result = g_key_file_get_string(config, "settings", "wordchars", NULL);

	if (result == NULL)
	{
		*wordchars = g_strdup(GEANY_WORDCHARS);
	}
	else
		*wordchars = result;
}


/* convert 0x..RRGGBB to 0x..BBGGRR */
static gint rotate_rgb(gint color)
{
	return ((color & 0xFF0000) >> 16) +
		(color & 0x00FF00) +
		((color & 0x0000FF) << 16);
}


static void get_keyfile_style(GKeyFile *config, GKeyFile *configh,
		const gchar *key_name, const HighlightingStyle *default_style, HighlightingStyle *style)
{
	gchar **list;
	gsize len;

	g_return_if_fail(config && configh && key_name && default_style && style);

	list = g_key_file_get_string_list(configh, "styling", key_name, &len, NULL);
	if (list == NULL)
		list = g_key_file_get_string_list(config, "styling", key_name, &len, NULL);

	if (list != NULL && list[0] != NULL)
		style->foreground = (gint) utils_strtod(list[0], NULL, FALSE);
	else
		style->foreground = rotate_rgb(default_style->foreground);

	if (list != NULL && list[1] != NULL)
		style->background = (gint) utils_strtod(list[1], NULL, FALSE);
	else
		style->background = rotate_rgb(default_style->background);

	if (list != NULL && list[2] != NULL) style->bold = utils_atob(list[2]);
	else style->bold = default_style->bold;

	if (list != NULL && list[3] != NULL) style->italic = utils_atob(list[3]);
	else style->italic = default_style->italic;

	g_strfreev(list);
}


static void get_keyfile_hex(GKeyFile *config, GKeyFile *configh,
				const gchar *section, const gchar *key,
				const gchar *foreground, const gchar *background, const gchar *bold,
				HighlightingStyle *style)
{
	gchar **list;
	gsize len;

	if (config == NULL || configh == NULL || section == NULL) return;

	list = g_key_file_get_string_list(configh, section, key, &len, NULL);
	if (list == NULL) list = g_key_file_get_string_list(config, section, key, &len, NULL);

	if (list != NULL && list[0] != NULL)
		style->foreground = (gint) utils_strtod(list[0], NULL, FALSE);
	else if (foreground)
		style->foreground = (gint) utils_strtod(foreground, NULL, FALSE);

	if (list != NULL && list[1] != NULL)
		style->background = (gint) utils_strtod(list[1], NULL, FALSE);
	else if (background)
		style->background = (gint) utils_strtod(background, NULL, FALSE);

	if (list != NULL && list[2] != NULL) style->bold = utils_atob(list[2]);
	else style->bold = utils_atob(bold);

	if (list != NULL && list[3] != NULL) style->italic = utils_atob(list[3]);
	else style->italic = FALSE;

	g_strfreev(list);
}


static void get_keyfile_int(GKeyFile *config, GKeyFile *configh, const gchar *section,
							const gchar *key, gint fdefault_val, gint sdefault_val,
							HighlightingStyle *style)
{
	gchar **list;
	gchar *end1, *end2;
	gsize len;

	if (config == NULL || configh == NULL || section == NULL) return;

	list = g_key_file_get_string_list(configh, section, key, &len, NULL);
	if (list == NULL) list = g_key_file_get_string_list(config, section, key, &len, NULL);

	if (list != NULL && list[0] != NULL) style->foreground = strtol(list[0], &end1, 10);
	else style->foreground = fdefault_val;
	if (list != NULL && list[1] != NULL) style->background = strtol(list[1], &end2, 10);
	else style->background = sdefault_val;

	/* if there was an error, strtol() returns 0 and end is list[x], so then we use default_val */
	if (list == NULL || list[0] == end1) style->foreground = fdefault_val;
	if (list == NULL || list[1] == end2) style->background = sdefault_val;

	g_strfreev(list);
}


static guint invert(guint icolour)
{
	if (common_style_set.invert_all)
	{
		guint r, g, b;

		r = 0xffffff - icolour;
		g = 0xffffff - (icolour >> 8);
		b = 0xffffff - (icolour >> 16);
		return (r | (g << 8) | (b << 16));
	}
	return icolour;
}


static void set_sci_style(ScintillaObject *sci, gint style, gint ft, gint styling_index)
{
	HighlightingStyle *style_ptr;

	if (ft == GEANY_FILETYPES_NONE)
		style_ptr = &common_style_set.styling[styling_index];
	else
		style_ptr = &style_sets[ft].styling[styling_index];

	SSM(sci, SCI_STYLESETFORE, style,	invert(style_ptr->foreground));
	SSM(sci, SCI_STYLESETBACK, style,	invert(style_ptr->background));
	SSM(sci, SCI_STYLESETBOLD, style,	style_ptr->bold);
	SSM(sci, SCI_STYLESETITALIC, style,	style_ptr->italic);
}


void highlighting_free_styles()
{
	gint i;

	for (i = 0; i < GEANY_MAX_BUILT_IN_FILETYPES - 1; i++)
	{
		StyleSet *style_ptr;
		style_ptr = &style_sets[i];

		g_free(style_ptr->styling);
		g_strfreev(style_ptr->keywords);
		g_free(style_ptr->wordchars);
	}
}


static GString *get_global_typenames(gint lang)
{
	GString *s = NULL;

	if (app->tm_workspace)
	{
		GPtrArray *tags_array = app->tm_workspace->global_tags;

		if (tags_array)
		{
			s = symbols_find_tags_as_string(tags_array, TM_GLOBAL_TYPE_MASK, lang);
		}
	}
	return s;
}


static gchar*
get_keyfile_whitespace_chars(GKeyFile *config, GKeyFile *configh)
{
	gchar *result;

	if (config == NULL || configh == NULL)
	{
		result = NULL;
	}
	else
	{
		result = g_key_file_get_string(configh, "settings", "whitespace_chars", NULL);
		if (result == NULL)
			result = g_key_file_get_string(config, "settings", "whitespace_chars", NULL);
	}
	if (result == NULL)
		result = g_strdup(GEANY_WHITESPACE_CHARS);
	return result;
}


static void styleset_common_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	static gboolean common_style_set_valid = FALSE;

	if (common_style_set_valid)
		return;
	common_style_set_valid = TRUE;	/* ensure filetypes.common is only loaded once */

	get_keyfile_hex(config, config_home, "styling", "default",
		"0x000000", "0xffffff", "false", &common_style_set.styling[GCS_DEFAULT]);
	get_keyfile_hex(config, config_home, "styling", "selection",
		"0xc0c0c0", "0x7f0000", "false", &common_style_set.styling[GCS_SELECTION]);
	get_keyfile_hex(config, config_home, "styling", "brace_good",
		"0x000000", "0xffffff", "false", &common_style_set.styling[GCS_BRACE_GOOD]);
	get_keyfile_hex(config, config_home, "styling", "brace_bad",
		"0xff0000", "0xffffff", "false", &common_style_set.styling[GCS_BRACE_BAD]);
	get_keyfile_hex(config, config_home, "styling", "margin_linenumber",
		"0x000000", "0xd0d0d0", "false", &common_style_set.styling[GCS_MARGIN_LINENUMBER]);
	get_keyfile_hex(config, config_home, "styling", "margin_folding",
		"0x000000", "0xdfdfdf", "false", &common_style_set.styling[GCS_MARGIN_FOLDING]);
	get_keyfile_hex(config, config_home, "styling", "current_line",
		"0x000000", "0xe5e5e5", "true", &common_style_set.styling[GCS_CURRENT_LINE]);
	get_keyfile_hex(config, config_home, "styling", "caret",
		"0x000000", "0x000000", "false", &common_style_set.styling[GCS_CARET]);
	get_keyfile_hex(config, config_home, "styling", "indent_guide",
		"0xc0c0c0", "0xffffff", "false", &common_style_set.styling[GCS_INDENT_GUIDE]);
	get_keyfile_hex(config, config_home, "styling", "white_space",
		"0xc0c0c0", "0xffffff", "true", &common_style_set.styling[GCS_WHITE_SPACE]);
	get_keyfile_hex(config, config_home, "styling", "marker_line",
		"0x000000", "0xffff00", "false", &common_style_set.styling[GCS_MARKER_LINE]);
	get_keyfile_hex(config, config_home, "styling", "marker_search",
		"0x000000", "0xB8F4B8", "false", &common_style_set.styling[GCS_MARKER_SEARCH]);
	{
		/* hack because get_keyfile_int uses a Style struct */
		HighlightingStyle tmp_style;
		get_keyfile_int(config, config_home, "styling", "folding_style",
			1, 1, &tmp_style);
		common_style_set.folding_style.marker = tmp_style.foreground;
		common_style_set.folding_style.lines = tmp_style.background;
		get_keyfile_int(config, config_home, "styling", "invert_all",
			0, 0, &tmp_style);
		common_style_set.invert_all = tmp_style.foreground;
		get_keyfile_int(config, config_home, "styling", "folding_horiz_line",
			2, 0, &tmp_style);
		common_style_set.folding_style.draw_line = tmp_style.foreground;
		get_keyfile_int(config, config_home, "styling", "caret_width",
			1, 0, &tmp_style);
		common_style_set.styling[GCS_CARET].background = tmp_style.foreground;
		get_keyfile_int(config, config_home, "styling", "line_wrap_visuals",
			3, 0, &tmp_style);
		common_style_set.styling[GCS_LINE_WRAP_VISUALS].foreground = tmp_style.foreground;
		common_style_set.styling[GCS_LINE_WRAP_VISUALS].background = tmp_style.background;
		get_keyfile_int(config, config_home, "styling", "line_wrap_indent",
			0, 0, &tmp_style);
		common_style_set.styling[GCS_LINE_WRAP_INDENT].foreground = tmp_style.foreground;
		get_keyfile_int(config, config_home, "styling", "translucency",
			256, 256, &tmp_style);
		common_style_set.styling[GCS_TRANSLUCENCY].foreground = tmp_style.foreground;
		common_style_set.styling[GCS_TRANSLUCENCY].background = tmp_style.background;
		get_keyfile_int(config, config_home, "styling", "marker_translucency",
			256, 256, &tmp_style);
		common_style_set.styling[GCS_MARKER_TRANSLUCENCY].foreground = tmp_style.foreground;
		common_style_set.styling[GCS_MARKER_TRANSLUCENCY].background = tmp_style.background;
	}

	get_keyfile_wordchars(config, config_home, &common_style_set.wordchars);
	whitespace_chars = get_keyfile_whitespace_chars(config, config_home);

}


static void styleset_common(ScintillaObject *sci, gint style_bits, filetype_id ft_id)
{
	SSM(sci, SCI_STYLECLEARALL, 0, 0);

	/* caret colour, style and width */
	SSM(sci, SCI_SETCARETFORE, invert(common_style_set.styling[GCS_CARET].foreground), 0);
	SSM(sci, SCI_SETCARETWIDTH, common_style_set.styling[GCS_CARET].background, 0);
	if (common_style_set.styling[GCS_CARET].bold)
		SSM(sci, SCI_SETCARETSTYLE, CARETSTYLE_BLOCK, 0);
	else
		SSM(sci, SCI_SETCARETSTYLE, CARETSTYLE_LINE, 0);

	/* colourize the current line */
	SSM(sci, SCI_SETCARETLINEBACK, invert(common_style_set.styling[GCS_CURRENT_LINE].background), 0);
	/* bold=enable current line */
	SSM(sci, SCI_SETCARETLINEVISIBLE, common_style_set.styling[GCS_CURRENT_LINE].bold, 0);

	/* Translucency for current line and selection */
	SSM(sci, SCI_SETCARETLINEBACKALPHA, common_style_set.styling[GCS_TRANSLUCENCY].foreground, 0);
	SSM(sci, SCI_SETSELALPHA, common_style_set.styling[GCS_TRANSLUCENCY].background, 0);

	/* line wrapping visuals */
	SSM(sci, SCI_SETWRAPVISUALFLAGS,
		common_style_set.styling[GCS_LINE_WRAP_VISUALS].foreground, 0);
	SSM(sci, SCI_SETWRAPVISUALFLAGSLOCATION,
		common_style_set.styling[GCS_LINE_WRAP_VISUALS].background, 0);
	SSM(sci, SCI_SETWRAPSTARTINDENT, common_style_set.styling[GCS_LINE_WRAP_INDENT].foreground, 0);

	/* indicator settings */
	SSM(sci, SCI_INDICSETSTYLE, 2, INDIC_SQUIGGLE);
	/* why? if I let this out, the indicator remains green with PHP */
	SSM(sci, SCI_INDICSETFORE, 0, invert(0x0000ff));
	SSM(sci, SCI_INDICSETFORE, 2, invert(0x0000ff));

	/* define marker symbols
	 * 0 -> line marker */
	SSM(sci, SCI_MARKERDEFINE, 0, SC_MARK_SHORTARROW);
	SSM(sci, SCI_MARKERSETFORE, 0, invert(common_style_set.styling[GCS_MARKER_LINE].foreground));
	SSM(sci, SCI_MARKERSETBACK, 0, invert(common_style_set.styling[GCS_MARKER_LINE].background));
	SSM(sci, SCI_MARKERSETALPHA, 0, common_style_set.styling[GCS_MARKER_TRANSLUCENCY].foreground);

	/* 1 -> user marker */
	SSM(sci, SCI_MARKERDEFINE, 1, SC_MARK_PLUS);
	SSM(sci, SCI_MARKERSETFORE, 1, invert(common_style_set.styling[GCS_MARKER_SEARCH].foreground));
	SSM(sci, SCI_MARKERSETBACK, 1, invert(common_style_set.styling[GCS_MARKER_SEARCH].background));
	SSM(sci, SCI_MARKERSETALPHA, 1, common_style_set.styling[GCS_MARKER_TRANSLUCENCY].background);

	/* 2 -> folding marker, other folding settings */
	SSM(sci, SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
	SSM(sci, SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);

	/* drawing a horizontal line when text if folded */
	switch (common_style_set.folding_style.draw_line)
	{
		case 1:
		{
			SSM(sci, SCI_SETFOLDFLAGS, 4, 0);
			break;
		}
		case 2:
		{
			SSM(sci, SCI_SETFOLDFLAGS, 16, 0);
			break;
		}
		default:
		{
			SSM(sci, SCI_SETFOLDFLAGS, 0, 0);
			break;
		}
	}

	/* choose the folding style - boxes or circles, I prefer boxes, so it is default ;-) */
	switch (common_style_set.folding_style.marker)
	{
		case 2:
		{
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEROPEN, SC_MARK_CIRCLEMINUS);
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDER, SC_MARK_CIRCLEPLUS);
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEREND, SC_MARK_CIRCLEPLUSCONNECTED);
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEROPENMID, SC_MARK_CIRCLEMINUSCONNECTED);
			break;
		}
		default:
		{
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
			break;
		}
	}

	/* choose the folding style - straight or curved, I prefer straight, so it is default ;-) */
	switch (common_style_set.folding_style.lines)
	{
		case 2:
		{
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNERCURVE);
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNERCURVE);
			break;
		}
		default:
		{
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);
			SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
			break;
		}
	}

	SSM(sci,SCI_MARKERDEFINE,  SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);

	SSM(sci,SCI_MARKERSETFORE, SC_MARKNUM_FOLDEROPEN, 0xffffff);
	SSM(sci,SCI_MARKERSETBACK, SC_MARKNUM_FOLDEROPEN, 0x000000);
	SSM(sci,SCI_MARKERSETFORE, SC_MARKNUM_FOLDER, 0xffffff);
	SSM(sci,SCI_MARKERSETBACK, SC_MARKNUM_FOLDER, 0x000000);
	SSM(sci,SCI_MARKERSETFORE, SC_MARKNUM_FOLDERSUB, 0xffffff);
	SSM(sci,SCI_MARKERSETBACK, SC_MARKNUM_FOLDERSUB, 0x000000);
	SSM(sci,SCI_MARKERSETFORE, SC_MARKNUM_FOLDERTAIL, 0xffffff);
	SSM(sci,SCI_MARKERSETBACK, SC_MARKNUM_FOLDERTAIL, 0x000000);
	SSM(sci,SCI_MARKERSETFORE, SC_MARKNUM_FOLDEREND, 0xffffff);
	SSM(sci,SCI_MARKERSETBACK, SC_MARKNUM_FOLDEREND, 0x000000);
	SSM(sci,SCI_MARKERSETFORE, SC_MARKNUM_FOLDEROPENMID, 0xffffff);
	SSM(sci,SCI_MARKERSETBACK, SC_MARKNUM_FOLDEROPENMID, 0x000000);
	SSM(sci,SCI_MARKERSETFORE, SC_MARKNUM_FOLDERMIDTAIL, 0xffffff);
	SSM(sci,SCI_MARKERSETBACK, SC_MARKNUM_FOLDERMIDTAIL, 0x000000);

	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold", (sptr_t) "1");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.compact", (sptr_t) "0");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.comment", (sptr_t) "1");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.preprocessor", (sptr_t) "1");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.at.else", (sptr_t) "1");


	/* bold (3rd argument) is whether to override default foreground selection */
	if (common_style_set.styling[GCS_SELECTION].bold)
		SSM(sci, SCI_SETSELFORE, 1, invert(common_style_set.styling[GCS_SELECTION].foreground));
	/* italic (4th argument) is whether to override default background selection */
	if (common_style_set.styling[GCS_SELECTION].italic)
		SSM(sci, SCI_SETSELBACK, 1, invert(common_style_set.styling[GCS_SELECTION].background));

	SSM(sci, SCI_SETSTYLEBITS, style_bits, 0);


	SSM(sci, SCI_SETFOLDMARGINCOLOUR, 1, invert(common_style_set.styling[GCS_MARGIN_FOLDING].background));
	/*SSM(sci, SCI_SETFOLDMARGINHICOLOUR, 1, invert(common_style_set.styling[GCS_MARGIN_FOLDING].background));*/
	set_sci_style(sci, STYLE_LINENUMBER, GEANY_FILETYPES_NONE, GCS_MARGIN_LINENUMBER);
	set_sci_style(sci, STYLE_BRACELIGHT, GEANY_FILETYPES_NONE, GCS_BRACE_GOOD);
	set_sci_style(sci, STYLE_BRACEBAD, GEANY_FILETYPES_NONE, GCS_BRACE_BAD);
	set_sci_style(sci, STYLE_INDENTGUIDE, GEANY_FILETYPES_NONE, GCS_INDENT_GUIDE);

	/* bold = common whitespace settings enabled */
	SSM(sci, SCI_SETWHITESPACEFORE, common_style_set.styling[GCS_WHITE_SPACE].bold,
		invert(common_style_set.styling[GCS_WHITE_SPACE].foreground));
	SSM(sci, SCI_SETWHITESPACEBACK, common_style_set.styling[GCS_WHITE_SPACE].italic,
		invert(common_style_set.styling[GCS_WHITE_SPACE].background));
}


/* Assign global typedefs and user secondary keywords */
static void assign_global_and_user_keywords(ScintillaObject *sci,
											const gchar *user_words, gint lang)
{
	GString *s;

	s = get_global_typenames(lang);
	if (s == NULL)
		s = g_string_sized_new(200);
	else
		g_string_append_c(s, ' '); /* append a space as delimiter to the existing list of words */

	g_string_append(s, user_words);

	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) s->str);
	g_string_free(s, TRUE);
}


/* All stylesets except None should call this. */
static void
apply_filetype_properties(ScintillaObject *sci, gint lexer, filetype_id ft_id)
{
	SSM(sci, SCI_SETLEXER, lexer, 0);

	SSM(sci, SCI_SETWORDCHARS, 0, (sptr_t) style_sets[ft_id].wordchars);
	/* have to set whitespace after setting wordchars */
	SSM(sci, SCI_SETWHITESPACECHARS, 0, (sptr_t) whitespace_chars);

	SSM(sci, SCI_AUTOCSETMAXHEIGHT, editor_prefs.symbolcompletion_max_height, 0);
}


/* Geany generic styles, initialized to defaults.
 * Ideally these would be used as common styling for all compilable programming
 * languages (and perhaps partially used for scripting languages too).
 * Currently only used as default styling for C-like languages. */
HighlightingStyle gsd_default =		{0x000000, 0xffffff, FALSE, FALSE};
HighlightingStyle gsd_comment =		{0xd00000, 0xffffff, FALSE, FALSE};
HighlightingStyle gsd_comment_doc =	{0x3f5fbf, 0xffffff, TRUE, FALSE};
HighlightingStyle gsd_number =		{0x007f00, 0xffffff, FALSE, FALSE};
HighlightingStyle gsd_reserved_word =	{0x00007f, 0xffffff, TRUE, FALSE};
HighlightingStyle gsd_system_word =	{0x991111, 0xffffff, TRUE, FALSE};
HighlightingStyle gsd_user_word =	{0x0000d0, 0xffffff, TRUE, FALSE};
HighlightingStyle gsd_string =		{0xff901e, 0xffffff, FALSE, FALSE};
HighlightingStyle gsd_pragma =		{0x007f7f, 0xffffff, FALSE, FALSE};
HighlightingStyle gsd_string_eol =	{0x000000, 0xe0c0e0, FALSE, FALSE};


/* call new_style_array(filetype_idx, >= 20) before using this. */
static void
styleset_c_like_init(GKeyFile *config, GKeyFile *config_home, gint filetype_idx)
{
	static HighlightingStyle uuid = {0x404080, 0xffffff, FALSE, FALSE};
	static HighlightingStyle operator = {0x301010, 0xffffff, FALSE, FALSE};
	static HighlightingStyle verbatim = {0x301010, 0xffffff, FALSE, FALSE};
	static HighlightingStyle regex = {0x105090, 0xffffff, FALSE, FALSE};

	StyleEntry entries[] =
	{
		{"default",		&gsd_default},
		{"comment",		&gsd_comment},
		{"commentline",	&gsd_comment},
		{"commentdoc",	&gsd_comment_doc},
		{"number",		&gsd_number},
		{"word",		&gsd_reserved_word},
		{"word2",		&gsd_system_word},
		{"string",		&gsd_string},
		{"character",	&gsd_string},
		{"uuid",		&uuid},
		{"preprocessor",&gsd_pragma},
		{"operator",	&operator},
		{"identifier",	&gsd_default},
		{"stringeol",	&gsd_string_eol},
		{"verbatim",	&verbatim},
		{"regex",		&regex},
		{"commentlinedoc", &gsd_comment_doc},
		{"commentdockeyword", &gsd_comment_doc},
		{"commentdockeyworderror", &gsd_comment_doc},
		{"globalclass",	&gsd_user_word}
	};
	gint i;

	for (i = 0; i < 20; i++)
		get_keyfile_style(config, config_home, entries[i].name, entries[i].style,
			&style_sets[filetype_idx].styling[i]);
}


static void styleset_c_like(ScintillaObject *sci, gint filetype_idx)
{
	set_sci_style(sci, STYLE_DEFAULT, filetype_idx, 0);
	set_sci_style(sci, SCE_C_DEFAULT, filetype_idx, 0);
	set_sci_style(sci, SCE_C_COMMENT, filetype_idx, 1);
	set_sci_style(sci, SCE_C_COMMENTLINE, filetype_idx, 2);
	set_sci_style(sci, SCE_C_COMMENTDOC, filetype_idx, 3);
	set_sci_style(sci, SCE_C_NUMBER, filetype_idx, 4);
	set_sci_style(sci, SCE_C_WORD, filetype_idx, 5);
	set_sci_style(sci, SCE_C_WORD2, filetype_idx, 6);
	set_sci_style(sci, SCE_C_STRING, filetype_idx, 7);
	set_sci_style(sci, SCE_C_CHARACTER, filetype_idx, 8);
	set_sci_style(sci, SCE_C_UUID, filetype_idx, 9);
	set_sci_style(sci, SCE_C_PREPROCESSOR, filetype_idx, 10);
	set_sci_style(sci, SCE_C_OPERATOR, filetype_idx, 11);
	set_sci_style(sci, SCE_C_IDENTIFIER, filetype_idx, 12);
	set_sci_style(sci, SCE_C_STRINGEOL, filetype_idx, 13);
	set_sci_style(sci, SCE_C_VERBATIM, filetype_idx, 14);
	set_sci_style(sci, SCE_C_REGEX, filetype_idx, 15);
	set_sci_style(sci, SCE_C_COMMENTLINEDOC, filetype_idx, 16);
	set_sci_style(sci, SCE_C_COMMENTDOCKEYWORD, filetype_idx, 17);
	set_sci_style(sci, SCE_C_COMMENTDOCKEYWORDERROR, filetype_idx, 18);
	/* is used for local structs and typedefs */
	set_sci_style(sci, SCE_C_GLOBALCLASS, filetype_idx, 19);
}


static void styleset_c_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_C, 21);
	styleset_c_like_init(config, config_home, GEANY_FILETYPES_C);
	get_keyfile_int(config, config_home, "styling", "styling_within_preprocessor",
		1, 0, &style_sets[GEANY_FILETYPES_C].styling[20]);

	style_sets[GEANY_FILETYPES_C].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_C, 0, "if const struct char int float double void long for while do case switch return");
	get_keyfile_keywords(config, config_home, "keywords", "secondary", GEANY_FILETYPES_C, 1, "");
	get_keyfile_keywords(config, config_home, "keywords", "docComment", GEANY_FILETYPES_C, 2, "TODO FIXME");
	style_sets[GEANY_FILETYPES_C].keywords[3] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_C].wordchars);
}


static void styleset_c(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_C;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_CPP, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_C].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_C].keywords[2]);

	/* assign global types, merge them with user defined keywords and set them */
	assign_global_and_user_keywords(sci, style_sets[GEANY_FILETYPES_C].keywords[1],
		filetypes[ft_id]->lang);

	styleset_c_like(sci, GEANY_FILETYPES_C);

	if (style_sets[GEANY_FILETYPES_C].styling[20].foreground == 1)
		SSM(sci, SCI_SETPROPERTY, (sptr_t) "styling.within.preprocessor", (sptr_t) "1");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "preprocessor.symbol.$(file.patterns.cpp)", (sptr_t) "#");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "preprocessor.start.$(file.patterns.cpp)", (sptr_t) "if ifdef ifndef");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "preprocessor.middle.$(file.patterns.cpp)", (sptr_t) "else elif");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "preprocessor.end.$(file.patterns.cpp)", (sptr_t) "endif");
}


static void styleset_cpp_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_CPP, 21);
	styleset_c_like_init(config, config_home, GEANY_FILETYPES_CPP);
	get_keyfile_int(config, config_home, "styling", "styling_within_preprocessor",
		1, 0, &style_sets[GEANY_FILETYPES_CPP].styling[20]);

	style_sets[GEANY_FILETYPES_CPP].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_CPP, 0, "and and_eq asm auto bitand bitor bool break case catch char class compl const const_cast continue default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new not not_eq operator or or_eq private protected public register reinterpret_cast return short signed sizeof static static_cast struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq");
	get_keyfile_keywords(config, config_home, "keywords", "secondary", GEANY_FILETYPES_CPP, 1, "");
	get_keyfile_keywords(config, config_home, "keywords", "docComment", GEANY_FILETYPES_CPP, 2, "TODO FIXME");
	style_sets[GEANY_FILETYPES_CPP].keywords[3] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_CPP].wordchars);
}


static void styleset_cpp(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_CPP;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_CPP, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_CPP].keywords[0]);
	/* for SCI_SETKEYWORDS = 1, see below*/
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_CPP].keywords[2]);

	/* assign global types, merge them with user defined keywords and set them */
	assign_global_and_user_keywords(sci, style_sets[GEANY_FILETYPES_CPP].keywords[1],
		filetypes[ft_id]->lang);

	styleset_c_like(sci, GEANY_FILETYPES_CPP);

	if (style_sets[GEANY_FILETYPES_CPP].styling[20].foreground == 1)
		SSM(sci, SCI_SETPROPERTY, (sptr_t) "styling.within.preprocessor", (sptr_t) "1");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "preprocessor.symbol.$(file.patterns.cpp)", (sptr_t) "#");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "preprocessor.start.$(file.patterns.cpp)", (sptr_t) "if ifdef ifndef");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "preprocessor.middle.$(file.patterns.cpp)", (sptr_t) "else elif");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "preprocessor.end.$(file.patterns.cpp)", (sptr_t) "endif");
}


static void styleset_cs_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_CS, 21);
	styleset_c_like_init(config, config_home, GEANY_FILETYPES_CS);
	get_keyfile_int(config, config_home, "styling", "styling_within_preprocessor",
		1, 0, &style_sets[GEANY_FILETYPES_CS].styling[20]);

	style_sets[GEANY_FILETYPES_CS].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_CS, 0, "\
			abstract as base bool break byte case catch char checked class \
			const continue decimal default delegate do double else enum \
			event explicit extern false finally fixed float for foreach goto if \
			implicit in int interface internal is lock long namespace new null \
			object operator out override params private protected public \
			readonly ref return sbyte sealed short sizeof stackalloc static \
			string struct switch this throw true try typeof uint ulong \
			unchecked unsafe ushort using virtual void volatile while");
	get_keyfile_keywords(config, config_home, "keywords", "secondary", GEANY_FILETYPES_CS, 1, "");
	get_keyfile_keywords(config, config_home, "keywords", "docComment", GEANY_FILETYPES_CS, 2, "");
	style_sets[GEANY_FILETYPES_CS].keywords[3] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_CS].wordchars);
}


static void styleset_cs(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_CS;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_CPP, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[ft_id].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[ft_id].keywords[2]);

	/* assign global types, merge them with user defined keywords and set them */
	assign_global_and_user_keywords(sci, style_sets[ft_id].keywords[1], filetypes[ft_id]->lang);

	styleset_c_like(sci, ft_id);

	if (style_sets[ft_id].styling[20].foreground == 1)
		SSM(sci, ft_id, (sptr_t) "styling.within.preprocessor", (sptr_t) "1");
}


static void styleset_pascal_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_PASCAL, 12);
	get_keyfile_hex(config, config_home, "styling", "default", "0x0000ff", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PASCAL].styling[0]);
	get_keyfile_style(config, config_home, "comment", &gsd_comment, &style_sets[GEANY_FILETYPES_PASCAL].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007F00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PASCAL].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_PASCAL].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PASCAL].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "character", "0x404000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PASCAL].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "preprocessor", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PASCAL].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PASCAL].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PASCAL].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "regex", "0x1b6313", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PASCAL].styling[9]);
	get_keyfile_style(config, config_home, "commentline", &gsd_comment, &style_sets[GEANY_FILETYPES_PASCAL].styling[10]);
	get_keyfile_style(config, config_home, "commentdoc", &gsd_comment_doc, &style_sets[GEANY_FILETYPES_PASCAL].styling[11]);

	style_sets[GEANY_FILETYPES_PASCAL].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_PASCAL, 0, "word integer char string byte real \
									for to do until repeat program if uses then else case var begin end \
									asm unit interface implementation procedure function object try class");
	style_sets[GEANY_FILETYPES_PASCAL].keywords[1] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_PASCAL].wordchars);
}


static void styleset_pascal(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_PASCAL;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_PASCAL, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_PASCAL].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_PASCAL, 0);
	set_sci_style(sci, SCE_C_DEFAULT, GEANY_FILETYPES_PASCAL, 0);
	set_sci_style(sci, SCE_C_COMMENT, GEANY_FILETYPES_PASCAL, 1);
	set_sci_style(sci, SCE_C_NUMBER, GEANY_FILETYPES_PASCAL, 2);
	set_sci_style(sci, SCE_C_WORD, GEANY_FILETYPES_PASCAL, 3);
	set_sci_style(sci, SCE_C_STRING, GEANY_FILETYPES_PASCAL, 4);
	set_sci_style(sci, SCE_C_CHARACTER, GEANY_FILETYPES_PASCAL, 5);
	set_sci_style(sci, SCE_C_PREPROCESSOR, GEANY_FILETYPES_PASCAL, 6);
	set_sci_style(sci, SCE_C_OPERATOR, GEANY_FILETYPES_PASCAL, 7);
	set_sci_style(sci, SCE_C_IDENTIFIER, GEANY_FILETYPES_PASCAL, 8);
	set_sci_style(sci, SCE_C_REGEX, GEANY_FILETYPES_PASCAL, 9);
	set_sci_style(sci, SCE_C_COMMENTLINE, GEANY_FILETYPES_PASCAL, 10);
	set_sci_style(sci, SCE_C_COMMENTDOC, GEANY_FILETYPES_PASCAL, 11);
}


static void styleset_makefile_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_MAKE, 7);
	get_keyfile_hex(config, config_home, "styling", "default", "0x00002f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_MAKE].styling[0]);
	get_keyfile_style(config, config_home, "comment", &gsd_comment, &style_sets[GEANY_FILETYPES_MAKE].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "preprocessor", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_MAKE].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_MAKE].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_MAKE].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "target", "0x0000ff", "0xffffff", "false", &style_sets[GEANY_FILETYPES_MAKE].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "ideol", "0x008000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_MAKE].styling[6]);

	style_sets[GEANY_FILETYPES_MAKE].keywords = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_MAKE].wordchars);
}


static void styleset_makefile(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_MAKE;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_MAKEFILE, ft_id);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_MAKE, 0);
	set_sci_style(sci, SCE_MAKE_DEFAULT, GEANY_FILETYPES_MAKE, 0);
	set_sci_style(sci, SCE_MAKE_COMMENT, GEANY_FILETYPES_MAKE, 1);
	set_sci_style(sci, SCE_MAKE_PREPROCESSOR, GEANY_FILETYPES_MAKE, 2);
	set_sci_style(sci, SCE_MAKE_IDENTIFIER, GEANY_FILETYPES_MAKE, 3);
	set_sci_style(sci, SCE_MAKE_OPERATOR, GEANY_FILETYPES_MAKE, 4);
	set_sci_style(sci, SCE_MAKE_TARGET, GEANY_FILETYPES_MAKE, 5);
	set_sci_style(sci, SCE_MAKE_IDEOL, GEANY_FILETYPES_MAKE, 6);
}


static void styleset_diff_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_DIFF, 7);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DIFF].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DIFF].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "command", "0x7f7f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DIFF].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "header", "0x7f0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DIFF].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "position", "0x00007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DIFF].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "deleted", "0xff2727", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DIFF].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "added", "0x34b034", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DIFF].styling[6]);

	style_sets[GEANY_FILETYPES_DIFF].keywords = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_DIFF].wordchars);
}


static void styleset_diff(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_DIFF;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_DIFF, ft_id);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_DIFF, 0);
	set_sci_style(sci, SCE_DIFF_DEFAULT, GEANY_FILETYPES_DIFF, 0);
	set_sci_style(sci, SCE_DIFF_COMMENT, GEANY_FILETYPES_DIFF, 1);
	set_sci_style(sci, SCE_DIFF_COMMAND, GEANY_FILETYPES_DIFF, 2);
	set_sci_style(sci, SCE_DIFF_HEADER, GEANY_FILETYPES_DIFF, 3);
	set_sci_style(sci, SCE_DIFF_POSITION, GEANY_FILETYPES_DIFF, 4);
	set_sci_style(sci, SCE_DIFF_DELETED, GEANY_FILETYPES_DIFF, 5);
	set_sci_style(sci, SCE_DIFF_ADDED, GEANY_FILETYPES_DIFF, 6);
}


static void styleset_latex_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_LATEX, 5);
	get_keyfile_hex(config, config_home, "styling", "default", "0x00002f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LATEX].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "command", "0xff0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_LATEX].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "tag", "0x007f7f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_LATEX].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "math", "0x00007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LATEX].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LATEX].styling[4]);

	style_sets[GEANY_FILETYPES_LATEX].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_LATEX, 0, "section subsection begin item");
	style_sets[GEANY_FILETYPES_LATEX].keywords[1] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_LATEX].wordchars);
}


static void styleset_latex(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_LATEX;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_LATEX, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_LATEX].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_LATEX, 0);
	set_sci_style(sci, SCE_L_DEFAULT, GEANY_FILETYPES_LATEX, 0);
	set_sci_style(sci, SCE_L_COMMAND, GEANY_FILETYPES_LATEX, 1);
	set_sci_style(sci, SCE_L_TAG, GEANY_FILETYPES_LATEX, 2);
	set_sci_style(sci, SCE_L_MATH, GEANY_FILETYPES_LATEX, 3);
	set_sci_style(sci, SCE_L_COMMENT, GEANY_FILETYPES_LATEX, 4);
}


static void styleset_php_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	style_sets[GEANY_FILETYPES_PHP].styling = NULL;
	style_sets[GEANY_FILETYPES_PHP].keywords = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_PHP].wordchars);
}


static void styleset_php(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_PHP;

	styleset_common(sci, 7, ft_id);

	apply_filetype_properties(sci, SCLEX_HTML, ft_id);

	SSM(sci, SCI_SETPROPERTY, (sptr_t) "phpscript.mode", (sptr_t) "1");

	/* use the same colouring as for XML */
	styleset_markup(sci, TRUE);
}


static void styleset_html_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	style_sets[GEANY_FILETYPES_HTML].styling = NULL;
	style_sets[GEANY_FILETYPES_HTML].keywords = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_HTML].wordchars);
}


static void styleset_html(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_HTML;

	styleset_common(sci, 7, ft_id);

	apply_filetype_properties(sci, SCLEX_HTML, ft_id);

	/* use the same colouring for HTML; XML and so on */
	styleset_markup(sci, TRUE);
}


static void styleset_markup_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_XML, 55);
	get_keyfile_hex(config, config_home, "styling", "html_default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "html_tag", "0x000099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "html_tagunknown", "0xff0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "html_attribute", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "html_attributeunknown", "0xff0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "html_number", "0x800080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "html_doublestring", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "html_singlestring", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "html_other", "0x800080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "html_comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "html_entity", "0x800080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "html_tagend", "0x000080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "html_xmlstart", "0x000099", "0xf0f0f0", "false", &style_sets[GEANY_FILETYPES_XML].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "html_xmlend", "0x000099", "0xf0f0f0", "false", &style_sets[GEANY_FILETYPES_XML].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "html_script", "0x000080", "0xf0f0f0", "false", &style_sets[GEANY_FILETYPES_XML].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "html_asp", "0x004f4f", "0xf0f0f0", "false", &style_sets[GEANY_FILETYPES_XML].styling[15]);
	get_keyfile_hex(config, config_home, "styling", "html_aspat", "0x004f4f", "0xf0f0f0", "false", &style_sets[GEANY_FILETYPES_XML].styling[16]);
	get_keyfile_hex(config, config_home, "styling", "html_cdata", "0x660099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[17]);
	get_keyfile_hex(config, config_home, "styling", "html_question", "0x0000ff", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[18]);
	get_keyfile_hex(config, config_home, "styling", "html_value", "0x660099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[19]);
	get_keyfile_hex(config, config_home, "styling", "html_xccomment", "0x660099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[20]);

	get_keyfile_hex(config, config_home, "styling", "sgml_default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[21]);
	get_keyfile_hex(config, config_home, "styling", "sgml_comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[22]);
	get_keyfile_hex(config, config_home, "styling", "sgml_special", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[23]);
	get_keyfile_hex(config, config_home, "styling", "sgml_command", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_XML].styling[24]);
	get_keyfile_hex(config, config_home, "styling", "sgml_doublestring", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[25]);
	get_keyfile_hex(config, config_home, "styling", "sgml_simplestring", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[26]);
	get_keyfile_hex(config, config_home, "styling", "sgml_1st_param", "0x404080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[27]);
	get_keyfile_hex(config, config_home, "styling", "sgml_entity", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[28]);
	get_keyfile_hex(config, config_home, "styling", "sgml_block_default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[29]);
	get_keyfile_hex(config, config_home, "styling", "sgml_1st_param_comment", "0x406090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[30]);
	get_keyfile_hex(config, config_home, "styling", "sgml_error", "0xff0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[31]);

	get_keyfile_hex(config, config_home, "styling", "php_default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[32]);
	get_keyfile_hex(config, config_home, "styling", "php_simplestring", "0x008000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[33]);
	get_keyfile_hex(config, config_home, "styling", "php_hstring", "0x008000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[34]);
	get_keyfile_hex(config, config_home, "styling", "php_number", "0x606000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[35]);
	get_keyfile_hex(config, config_home, "styling", "php_word", "0x000099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[36]);
	get_keyfile_hex(config, config_home, "styling", "php_variable", "0x7f0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[37]);
	get_keyfile_hex(config, config_home, "styling", "php_comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[38]);
	get_keyfile_hex(config, config_home, "styling", "php_commentline", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[39]);
	get_keyfile_hex(config, config_home, "styling", "php_operator", "0x102060", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[40]);
	get_keyfile_hex(config, config_home, "styling", "php_hstring_variable", "0x101060", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[41]);
	get_keyfile_hex(config, config_home, "styling", "php_complex_variable", "0x105010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[42]);

	get_keyfile_hex(config, config_home, "styling", "jscript_start", "0x008080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[43]);
	get_keyfile_hex(config, config_home, "styling", "jscript_default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[44]);
	get_keyfile_hex(config, config_home, "styling", "jscript_comment", "0xd00000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[45]);
	get_keyfile_hex(config, config_home, "styling", "jscript_commentline", "0xd00000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[46]);
	get_keyfile_hex(config, config_home, "styling", "jscript_commentdoc", "0x3f5fbf", "0xffffff", "true", &style_sets[GEANY_FILETYPES_XML].styling[47]);
	get_keyfile_hex(config, config_home, "styling", "jscript_number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[48]);
	get_keyfile_hex(config, config_home, "styling", "jscript_word", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[49]);
	get_keyfile_hex(config, config_home, "styling", "jscript_keyword", "0x00007f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_XML].styling[50]);
	get_keyfile_hex(config, config_home, "styling", "jscript_doublestring", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[51]);
	get_keyfile_hex(config, config_home, "styling", "jscript_singlestring", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[52]);
	get_keyfile_hex(config, config_home, "styling", "jscript_symbols", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_XML].styling[53]);
	get_keyfile_hex(config, config_home, "styling", "jscript_stringeol", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_XML].styling[54]);

	style_sets[GEANY_FILETYPES_XML].keywords = g_new(gchar*, 7);
	get_keyfile_keywords(config, config_home, "keywords", "html", GEANY_FILETYPES_XML, 0, "a abbr acronym address applet area b base basefont bdo big blockquote body br button caption center cite code col colgroup dd del dfn dir div dl dt em embed fieldset font form frame frameset h1 h2 h3 h4 h5 h6 head hr html i iframe img input ins isindex kbd label legend li link map menu meta noframes noscript object ol optgroup option p param pre q quality s samp script select small span strike strong style sub sup table tbody td textarea tfoot th thead title tr tt u ul var xmlns leftmargin topmargin abbr accept-charset accept accesskey action align alink alt archive axis background bgcolor border cellpadding cellspacing char charoff charset checked cite class classid clear codebase codetype color cols colspan compact content coords data datafld dataformatas datapagesize datasrc datetime declare defer dir disabled enctype face for frame frameborder selected headers height href hreflang hspace http-equiv id ismap label lang language link longdesc marginwidth marginheight maxlength media framespacing method multiple name nohref noresize noshade nowrap object onblur onchange onclick ondblclick onfocus onkeydown onkeypress onkeyup onload onmousedown onmousemove onmouseover onmouseout onmouseup onreset onselect onsubmit onunload profile prompt pluginspage readonly rel rev rows rowspan rules scheme scope scrolling shape size span src standby start style summary tabindex target text title type usemap valign value valuetype version vlink vspace width text password checkbox radio submit reset file hidden image public doctype xml");
	get_keyfile_keywords(config, config_home, "keywords", "javascript", GEANY_FILETYPES_XML, 1, "abs abstract acos anchor asin atan atan2 big bold boolean break byte case catch ceil char charAt charCodeAt class concat const continue cos Date debugger default delete do double else enum escape eval exp export extends false final finally fixed float floor fontcolor fontsize for fromCharCode function goto if implements import in indexOf Infinity instanceof int interface isFinite isNaN italics join lastIndexOf length link log long Math max MAX_VALUE min MIN_VALUE NaN native NEGATIVE_INFINITY new null Number package parseFloat parseInt pop POSITIVE_INFINITY pow private protected public push random return reverse round shift short sin slice small sort splice split sqrt static strike string String sub substr substring sup super switch synchronized tan this throw throws toLowerCase toString toUpperCase transient true try typeof undefined unescape unshift valueOf var void volatile while with");
	get_keyfile_keywords(config, config_home, "keywords", "vbscript", GEANY_FILETYPES_XML, 2, "and as byref byval case call const continue dim do each else elseif end error exit false for function global goto if in loop me new next not nothing on optional or private public redim rem resume select set sub then to true type while with boolean byte currency date double integer long object single string type variant");
	get_keyfile_keywords(config, config_home, "keywords", "python", GEANY_FILETYPES_XML, 3, "and as assert break class continue def del elif else except exec finally for from global if import in is lambda not or pass print raise return try while with yield False None True");
	get_keyfile_keywords(config, config_home, "keywords", "php", GEANY_FILETYPES_XML, 4, "abstract and array as bool boolean break case catch cfunction __class__ class clone const continue declare default die directory do double echo else elseif empty enddeclare endfor endforeach endif endswitch endwhile eval exception exit extends false __file__ final float for foreach __function__ function global if implements include include_once int integer interface isset __line__ list __method__ new null object old_function or parent php_user_filter print private protected public real require require_once resource return __sleep static stdclass string switch this throw true try unset use var __wakeup while xor");
	get_keyfile_keywords(config, config_home, "keywords", "sgml", GEANY_FILETYPES_XML, 5, "ELEMENT DOCTYPE ATTLIST ENTITY NOTATION");
	style_sets[GEANY_FILETYPES_XML].keywords[6] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_XML].wordchars);
}


static void styleset_markup(ScintillaObject *sci, gboolean set_keywords)
{
	/* Used by several filetypes */
	if (style_sets[GEANY_FILETYPES_XML].styling == NULL)
		filetypes_load_config(GEANY_FILETYPES_XML);

	/* manually initialise filetype Python for use with embedded Python */
	filetypes_load_config(GEANY_FILETYPES_PYTHON);

	/* don't set keywords for plain XML */
	if (set_keywords)
	{
		SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_XML].keywords[0]);
		SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_XML].keywords[1]);
		SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_XML].keywords[2]);
		SSM(sci, SCI_SETKEYWORDS, 3, (sptr_t) style_sets[GEANY_FILETYPES_XML].keywords[3]);
		SSM(sci, SCI_SETKEYWORDS, 4, (sptr_t) style_sets[GEANY_FILETYPES_XML].keywords[4]);
	}
	SSM(sci, SCI_SETKEYWORDS, 5, (sptr_t) style_sets[GEANY_FILETYPES_XML].keywords[5]);

	/* hotspotting, nice thing */
	SSM(sci, SCI_SETHOTSPOTACTIVEFORE, 1, invert(0xff0000));
	SSM(sci, SCI_SETHOTSPOTACTIVEUNDERLINE, 1, 0);
	SSM(sci, SCI_SETHOTSPOTSINGLELINE, 1, 0);
	SSM(sci, SCI_STYLESETHOTSPOT, SCE_H_QUESTION, 1);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_XML, 0);
	set_sci_style(sci, SCE_H_DEFAULT, GEANY_FILETYPES_XML, 0);
	set_sci_style(sci, SCE_H_TAG, GEANY_FILETYPES_XML, 1);
	set_sci_style(sci, SCE_H_TAGUNKNOWN, GEANY_FILETYPES_XML, 2);
	set_sci_style(sci, SCE_H_ATTRIBUTE, GEANY_FILETYPES_XML, 3);
	set_sci_style(sci, SCE_H_ATTRIBUTEUNKNOWN, GEANY_FILETYPES_XML, 4);
	set_sci_style(sci, SCE_H_NUMBER, GEANY_FILETYPES_XML, 5);
	set_sci_style(sci, SCE_H_DOUBLESTRING, GEANY_FILETYPES_XML, 6);
	set_sci_style(sci, SCE_H_SINGLESTRING, GEANY_FILETYPES_XML, 7);
	set_sci_style(sci, SCE_H_OTHER, GEANY_FILETYPES_XML, 8);
	set_sci_style(sci, SCE_H_COMMENT, GEANY_FILETYPES_XML, 9);
	set_sci_style(sci, SCE_H_ENTITY, GEANY_FILETYPES_XML, 10);
	set_sci_style(sci, SCE_H_TAGEND, GEANY_FILETYPES_XML, 11);
	SSM(sci, SCI_STYLESETEOLFILLED, SCE_H_XMLSTART, 1);
	set_sci_style(sci, SCE_H_XMLSTART, GEANY_FILETYPES_XML, 12);
	set_sci_style(sci, SCE_H_XMLEND, GEANY_FILETYPES_XML, 13);
	set_sci_style(sci, SCE_H_SCRIPT, GEANY_FILETYPES_XML, 14);
	SSM(sci, SCI_STYLESETEOLFILLED, SCE_H_ASP, 1);
	set_sci_style(sci, SCE_H_ASP, GEANY_FILETYPES_XML, 15);
	SSM(sci, SCI_STYLESETEOLFILLED, SCE_H_ASPAT, 1);
	set_sci_style(sci, SCE_H_ASPAT, GEANY_FILETYPES_XML, 16);
	set_sci_style(sci, SCE_H_CDATA, GEANY_FILETYPES_XML, 17);
	set_sci_style(sci, SCE_H_QUESTION, GEANY_FILETYPES_XML, 18);
	set_sci_style(sci, SCE_H_VALUE, GEANY_FILETYPES_XML, 19);
	set_sci_style(sci, SCE_H_XCCOMMENT, GEANY_FILETYPES_XML, 20);

	set_sci_style(sci, SCE_H_SGML_DEFAULT, GEANY_FILETYPES_XML, 21);
	set_sci_style(sci, SCE_H_SGML_COMMENT, GEANY_FILETYPES_XML, 22);
	set_sci_style(sci, SCE_H_SGML_SPECIAL, GEANY_FILETYPES_XML, 23);
	set_sci_style(sci, SCE_H_SGML_COMMAND, GEANY_FILETYPES_XML, 24);
	set_sci_style(sci, SCE_H_SGML_DOUBLESTRING, GEANY_FILETYPES_XML, 25);
	set_sci_style(sci, SCE_H_SGML_SIMPLESTRING, GEANY_FILETYPES_XML, 26);
	set_sci_style(sci, SCE_H_SGML_1ST_PARAM, GEANY_FILETYPES_XML, 27);
	set_sci_style(sci, SCE_H_SGML_ENTITY, GEANY_FILETYPES_XML, 28);
	set_sci_style(sci, SCE_H_SGML_BLOCK_DEFAULT, GEANY_FILETYPES_XML, 29);
	set_sci_style(sci, SCE_H_SGML_1ST_PARAM_COMMENT, GEANY_FILETYPES_XML, 30);
	set_sci_style(sci, SCE_H_SGML_ERROR, GEANY_FILETYPES_XML, 31);

	/* embedded JavaScript */
	set_sci_style(sci, SCE_HJ_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HJ_DEFAULT, GEANY_FILETYPES_XML, 44);
	set_sci_style(sci, SCE_HJ_COMMENT, GEANY_FILETYPES_XML, 45);
	set_sci_style(sci, SCE_HJ_COMMENTLINE, GEANY_FILETYPES_XML, 46);
	set_sci_style(sci, SCE_HJ_COMMENTDOC, GEANY_FILETYPES_XML, 47);
	set_sci_style(sci, SCE_HJ_NUMBER, GEANY_FILETYPES_XML, 48);
	set_sci_style(sci, SCE_HJ_WORD, GEANY_FILETYPES_XML, 49);
	set_sci_style(sci, SCE_HJ_KEYWORD, GEANY_FILETYPES_XML, 50);
	set_sci_style(sci, SCE_HJ_DOUBLESTRING, GEANY_FILETYPES_XML, 51);
	set_sci_style(sci, SCE_HJ_SINGLESTRING, GEANY_FILETYPES_XML, 52);
	set_sci_style(sci, SCE_HJ_SYMBOLS, GEANY_FILETYPES_XML, 53);
	set_sci_style(sci, SCE_HJ_STRINGEOL, GEANY_FILETYPES_XML, 54);

	/* for HB, VBScript?, use the same styles as for JavaScript */
	set_sci_style(sci, SCE_HB_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HB_DEFAULT, GEANY_FILETYPES_XML, 44);
	set_sci_style(sci, SCE_HB_COMMENTLINE, GEANY_FILETYPES_XML, 46);
	set_sci_style(sci, SCE_HB_NUMBER, GEANY_FILETYPES_XML, 48);
	set_sci_style(sci, SCE_HB_WORD, GEANY_FILETYPES_XML, 49);
	set_sci_style(sci, SCE_HB_STRING, GEANY_FILETYPES_XML, 51);
	set_sci_style(sci, SCE_HB_IDENTIFIER, GEANY_FILETYPES_XML, 53);
	set_sci_style(sci, SCE_HB_STRINGEOL, GEANY_FILETYPES_XML, 54);

	/* for HBA, VBScript?, use the same styles as for JavaScript */
	set_sci_style(sci, SCE_HBA_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HBA_DEFAULT, GEANY_FILETYPES_XML, 44);
	set_sci_style(sci, SCE_HBA_COMMENTLINE, GEANY_FILETYPES_XML, 46);
	set_sci_style(sci, SCE_HBA_NUMBER, GEANY_FILETYPES_XML, 48);
	set_sci_style(sci, SCE_HBA_WORD, GEANY_FILETYPES_XML, 49);
	set_sci_style(sci, SCE_HBA_STRING, GEANY_FILETYPES_XML, 51);
	set_sci_style(sci, SCE_HBA_IDENTIFIER, GEANY_FILETYPES_XML, 53);
	set_sci_style(sci, SCE_HBA_STRINGEOL, GEANY_FILETYPES_XML, 54);

	/* for HJA, ASP Javascript, use the same styles as for JavaScript */
	set_sci_style(sci, SCE_HJA_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HJA_DEFAULT, GEANY_FILETYPES_XML, 44);
	set_sci_style(sci, SCE_HJA_COMMENT, GEANY_FILETYPES_XML, 45);
	set_sci_style(sci, SCE_HJA_COMMENTLINE, GEANY_FILETYPES_XML, 46);
	set_sci_style(sci, SCE_HJA_COMMENTDOC, GEANY_FILETYPES_XML, 47);
	set_sci_style(sci, SCE_HJA_NUMBER, GEANY_FILETYPES_XML, 48);
	set_sci_style(sci, SCE_HJA_WORD, GEANY_FILETYPES_XML, 49);
	set_sci_style(sci, SCE_HJA_KEYWORD, GEANY_FILETYPES_XML, 50);
	set_sci_style(sci, SCE_HJA_DOUBLESTRING, GEANY_FILETYPES_XML, 51);
	set_sci_style(sci, SCE_HJA_SINGLESTRING, GEANY_FILETYPES_XML, 52);
	set_sci_style(sci, SCE_HJA_SYMBOLS, GEANY_FILETYPES_XML, 53);
	set_sci_style(sci, SCE_HJA_STRINGEOL, GEANY_FILETYPES_XML, 54);

	/* for embedded Python we use the Python styles */
	set_sci_style(sci, SCE_HP_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HP_DEFAULT, GEANY_FILETYPES_PYTHON, 0);
	set_sci_style(sci, SCE_HP_COMMENTLINE, GEANY_FILETYPES_PYTHON, 1);
	set_sci_style(sci, SCE_HP_NUMBER, GEANY_FILETYPES_PYTHON, 2);
	set_sci_style(sci, SCE_HP_STRING, GEANY_FILETYPES_PYTHON, 3);
	set_sci_style(sci, SCE_HP_CHARACTER, GEANY_FILETYPES_PYTHON, 4);
	set_sci_style(sci, SCE_HP_WORD, GEANY_FILETYPES_PYTHON, 5);
	set_sci_style(sci, SCE_HP_TRIPLE, GEANY_FILETYPES_PYTHON, 6);
	set_sci_style(sci, SCE_HP_TRIPLEDOUBLE, GEANY_FILETYPES_PYTHON, 7);
	set_sci_style(sci, SCE_HP_CLASSNAME, GEANY_FILETYPES_PYTHON, 8);
	set_sci_style(sci, SCE_HP_DEFNAME, GEANY_FILETYPES_PYTHON, 9);
	set_sci_style(sci, SCE_HP_OPERATOR, GEANY_FILETYPES_PYTHON, 10);
	set_sci_style(sci, SCE_HP_IDENTIFIER, GEANY_FILETYPES_PYTHON, 11);

	/* for embedded HPA (what is this?) we use the Python styles */
	set_sci_style(sci, SCE_HPA_START, GEANY_FILETYPES_XML, 43);
	set_sci_style(sci, SCE_HPA_DEFAULT, GEANY_FILETYPES_PYTHON, 0);
	set_sci_style(sci, SCE_HPA_COMMENTLINE, GEANY_FILETYPES_PYTHON, 1);
	set_sci_style(sci, SCE_HPA_NUMBER, GEANY_FILETYPES_PYTHON, 2);
	set_sci_style(sci, SCE_HPA_STRING, GEANY_FILETYPES_PYTHON, 3);
	set_sci_style(sci, SCE_HPA_CHARACTER, GEANY_FILETYPES_PYTHON, 4);
	set_sci_style(sci, SCE_HPA_WORD, GEANY_FILETYPES_PYTHON, 5);
	set_sci_style(sci, SCE_HPA_TRIPLE, GEANY_FILETYPES_PYTHON, 6);
	set_sci_style(sci, SCE_HPA_TRIPLEDOUBLE, GEANY_FILETYPES_PYTHON, 7);
	set_sci_style(sci, SCE_HPA_CLASSNAME, GEANY_FILETYPES_PYTHON, 8);
	set_sci_style(sci, SCE_HPA_DEFNAME, GEANY_FILETYPES_PYTHON, 9);
	set_sci_style(sci, SCE_HPA_OPERATOR, GEANY_FILETYPES_PYTHON, 10);
	set_sci_style(sci, SCE_HPA_IDENTIFIER, GEANY_FILETYPES_PYTHON, 11);

	/* PHP */
	set_sci_style(sci, SCE_HPHP_DEFAULT, GEANY_FILETYPES_XML, 32);
	set_sci_style(sci, SCE_HPHP_SIMPLESTRING, GEANY_FILETYPES_XML, 33);
	set_sci_style(sci, SCE_HPHP_HSTRING, GEANY_FILETYPES_XML, 34);
	set_sci_style(sci, SCE_HPHP_NUMBER, GEANY_FILETYPES_XML, 35);
	set_sci_style(sci, SCE_HPHP_WORD, GEANY_FILETYPES_XML, 36);
	set_sci_style(sci, SCE_HPHP_VARIABLE, GEANY_FILETYPES_XML, 37);
	set_sci_style(sci, SCE_HPHP_COMMENT, GEANY_FILETYPES_XML, 38);
	set_sci_style(sci, SCE_HPHP_COMMENTLINE, GEANY_FILETYPES_XML, 39);
	set_sci_style(sci, SCE_HPHP_OPERATOR, GEANY_FILETYPES_XML, 40);
	set_sci_style(sci, SCE_HPHP_HSTRING_VARIABLE, GEANY_FILETYPES_XML, 41);
	set_sci_style(sci, SCE_HPHP_COMPLEX_VARIABLE, GEANY_FILETYPES_XML, 42);

	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.html", (sptr_t) "1");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.html.preprocessor", (sptr_t) "1");
}


static void styleset_java_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_JAVA, 20);
	styleset_c_like_init(config, config_home, GEANY_FILETYPES_JAVA);

	style_sets[GEANY_FILETYPES_JAVA].keywords = g_new(gchar*, 5);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_JAVA, 0, "\
										abstract assert break case catch class \
										const continue default do else extends final finally for future \
										generic goto if implements import inner instanceof interface \
										native new outer package private protected public rest \
										return static super switch synchronized this throw throws \
										transient try var volatile while true false null");
	get_keyfile_keywords(config, config_home, "keywords", "secondary", GEANY_FILETYPES_JAVA, 1, "boolean byte char double float int long null short void");
	get_keyfile_keywords(config, config_home, "keywords", "doccomment", GEANY_FILETYPES_JAVA, 2, "return param author throws");
	get_keyfile_keywords(config, config_home, "keywords", "typedefs", GEANY_FILETYPES_JAVA, 3, "");
	style_sets[GEANY_FILETYPES_JAVA].keywords[4] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_JAVA].wordchars);
}


static void styleset_java(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_JAVA;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_CPP, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_JAVA].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_JAVA].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_JAVA].keywords[2]);
	SSM(sci, SCI_SETKEYWORDS, 4, (sptr_t) style_sets[GEANY_FILETYPES_JAVA].keywords[3]);

	styleset_c_like(sci, GEANY_FILETYPES_JAVA);
}


static void styleset_perl_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_PERL, 30);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "error", "0xff0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[1]);
	get_keyfile_style(config, config_home, "commentline", &gsd_comment, &style_sets[GEANY_FILETYPES_PERL].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_PERL].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "character", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "preprocessor", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "scalar", "0x7f0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "pod", "0x035650", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "regex", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "array", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "hash", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "symboltable", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[15]);
	get_keyfile_hex(config, config_home, "styling", "backticks", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_PERL].styling[16]);
	get_keyfile_hex(config, config_home, "styling", "pod_verbatim", "0x004000", "0xc0ffc0", "false", &style_sets[GEANY_FILETYPES_PERL].styling[17]);
	get_keyfile_hex(config, config_home, "styling", "reg_subst", "0x000000", "0xf0e080", "false", &style_sets[GEANY_FILETYPES_PERL].styling[18]);
	get_keyfile_hex(config, config_home, "styling", "datasection", "0x600000", "0xfff0d8", "false", &style_sets[GEANY_FILETYPES_PERL].styling[19]);
	get_keyfile_hex(config, config_home, "styling", "here_delim", "0x000000", "0xddd0dd", "false", &style_sets[GEANY_FILETYPES_PERL].styling[20]);
	get_keyfile_hex(config, config_home, "styling", "here_q", "0x7f007f", "0xddd0dd", "false", &style_sets[GEANY_FILETYPES_PERL].styling[21]);
	get_keyfile_hex(config, config_home, "styling", "here_qq", "0x7f007f", "0xddd0dd", "true", &style_sets[GEANY_FILETYPES_PERL].styling[22]);
	get_keyfile_hex(config, config_home, "styling", "here_qx", "0x7f007f", "0xddd0dd", "true", &style_sets[GEANY_FILETYPES_PERL].styling[23]);
	get_keyfile_hex(config, config_home, "styling", "string_q", "0x7f007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[24]);
	get_keyfile_hex(config, config_home, "styling", "string_qq", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[25]);
	get_keyfile_hex(config, config_home, "styling", "string_qx", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_PERL].styling[26]);
	get_keyfile_hex(config, config_home, "styling", "string_qr", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[27]);
	get_keyfile_hex(config, config_home, "styling", "string_qw", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[28]);
	get_keyfile_hex(config, config_home, "styling", "variable_indexer", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PERL].styling[29]);

	style_sets[GEANY_FILETYPES_PERL].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_PERL, 0, "\
									NULL __FILE__ __LINE__ __PACKAGE__ __DATA__ __END__ AUTOLOAD \
									BEGIN CORE DESTROY END EQ GE GT INIT LE LT NE CHECK abs accept \
									alarm and atan2 bind binmode bless caller chdir chmod chomp chop \
									chown chr chroot close closedir cmp connect continue cos crypt \
									dbmclose dbmopen defined delete die do dump each else elsif endgrent \
									endhostent endnetent endprotoent endpwent endservent eof eq eval \
									exec exists exit exp fcntl fileno flock for foreach fork format \
									formline ge getc getgrent getgrgid getgrnam gethostbyaddr gethostbyname \
									gethostent getlogin getnetbyaddr getnetbyname getnetent getpeername \
									getpgrp getppid getpriority getprotobyname getprotobynumber getprotoent \
									getpwent getpwnam getpwuid getservbyname getservbyport getservent \
									getsockname getsockopt glob gmtime goto grep gt hex if index \
									int ioctl join keys kill last lc lcfirst le length link listen \
									local localtime lock log lstat lt m map mkdir msgctl msgget msgrcv \
									msgsnd my ne next no not oct open opendir or ord our pack package \
									pipe pop pos print printf prototype push q qq qr quotemeta qu \
									qw qx rand read readdir readline readlink readpipe recv redo \
									ref rename require reset return reverse rewinddir rindex rmdir \
									s scalar seek seekdir select semctl semget semop send setgrent \
									sethostent setnetent setpgrp setpriority setprotoent setpwent \
									setservent setsockopt shift shmctl shmget shmread shmwrite shutdown \
									sin sleep socket socketpair sort splice split sprintf sqrt srand \
									stat study sub substr symlink syscall sysopen sysread sysseek \
									system syswrite tell telldir tie tied time times tr truncate \
									uc ucfirst umask undef unless unlink unpack unshift untie until \
									use utime values vec wait waitpid wantarray warn while write \
									x xor y");
	style_sets[GEANY_FILETYPES_PERL].keywords[1] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_PERL].wordchars);
}


static void styleset_perl(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_PERL;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_PERL, ft_id);

	SSM(sci, SCI_SETPROPERTY, (sptr_t) "styling.within.preprocessor", (sptr_t) "1");

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_PERL].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_PERL, 0);
	set_sci_style(sci, SCE_PL_DEFAULT, GEANY_FILETYPES_PERL, 0);
	set_sci_style(sci, SCE_PL_ERROR, GEANY_FILETYPES_PERL, 1);
	set_sci_style(sci, SCE_PL_COMMENTLINE, GEANY_FILETYPES_PERL, 2);
	set_sci_style(sci, SCE_PL_NUMBER, GEANY_FILETYPES_PERL, 3);
	set_sci_style(sci, SCE_PL_WORD, GEANY_FILETYPES_PERL, 4);
	set_sci_style(sci, SCE_PL_STRING, GEANY_FILETYPES_PERL, 5);
	set_sci_style(sci, SCE_PL_CHARACTER, GEANY_FILETYPES_PERL, 6);
	set_sci_style(sci, SCE_PL_PREPROCESSOR, GEANY_FILETYPES_PERL, 7);
	set_sci_style(sci, SCE_PL_OPERATOR, GEANY_FILETYPES_PERL, 8);
	set_sci_style(sci, SCE_PL_IDENTIFIER, GEANY_FILETYPES_PERL, 9);
	set_sci_style(sci, SCE_PL_SCALAR, GEANY_FILETYPES_PERL, 10);
	set_sci_style(sci, SCE_PL_POD, GEANY_FILETYPES_PERL, 11);
	set_sci_style(sci, SCE_PL_REGEX, GEANY_FILETYPES_PERL, 12);
	set_sci_style(sci, SCE_PL_ARRAY, GEANY_FILETYPES_PERL, 13);
	set_sci_style(sci, SCE_PL_HASH, GEANY_FILETYPES_PERL, 14);
	set_sci_style(sci, SCE_PL_SYMBOLTABLE, GEANY_FILETYPES_PERL, 15);
	set_sci_style(sci, SCE_PL_BACKTICKS, GEANY_FILETYPES_PERL, 16);
	set_sci_style(sci, SCE_PL_POD_VERB, GEANY_FILETYPES_PERL, 17);
	set_sci_style(sci, SCE_PL_REGSUBST, GEANY_FILETYPES_PERL, 18);
	set_sci_style(sci, SCE_PL_DATASECTION, GEANY_FILETYPES_PERL, 19);
	set_sci_style(sci, SCE_PL_HERE_DELIM, GEANY_FILETYPES_PERL, 20);
	set_sci_style(sci, SCE_PL_HERE_Q, GEANY_FILETYPES_PERL, 21);
	set_sci_style(sci, SCE_PL_HERE_QQ, GEANY_FILETYPES_PERL, 22);
	set_sci_style(sci, SCE_PL_HERE_QX, GEANY_FILETYPES_PERL, 23);
	set_sci_style(sci, SCE_PL_STRING_Q, GEANY_FILETYPES_PERL, 24);
	set_sci_style(sci, SCE_PL_STRING_QQ, GEANY_FILETYPES_PERL, 25);
	set_sci_style(sci, SCE_PL_STRING_QX, GEANY_FILETYPES_PERL, 26);
	set_sci_style(sci, SCE_PL_STRING_QR, GEANY_FILETYPES_PERL, 27);
	set_sci_style(sci, SCE_PL_STRING_QW, GEANY_FILETYPES_PERL, 28);
	set_sci_style(sci, SCE_PL_VARIABLE_INDEXER, GEANY_FILETYPES_PERL, 29);
}


static void styleset_python_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_PYTHON, 16);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "commentline", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x400080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "string", "0x008000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "character", "0x008000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x600080", "0xffffff", "true", &style_sets[GEANY_FILETYPES_PYTHON].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "triple", "0x008020", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "tripledouble", "0x404000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "classname", "0x003030", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "defname", "0x000080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x300080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "commentblock", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "stringeol", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "word2", "0xdd00a6", "0xffffff", "true", &style_sets[GEANY_FILETYPES_PYTHON].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "decorator", "0x808000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_PYTHON].styling[15]);

	style_sets[GEANY_FILETYPES_PYTHON].keywords = g_new(gchar*, 3);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_PYTHON, 0, "and as assert break class continue def del elif else except exec finally for from global if import in is lambda not or pass print raise return try while with yield False None True");
	get_keyfile_keywords(config, config_home, "keywords", "identifiers", GEANY_FILETYPES_PYTHON, 1, "");
	style_sets[GEANY_FILETYPES_PYTHON].keywords[2] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_PYTHON].wordchars);
}


static void styleset_python(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_PYTHON;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_PYTHON, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_PYTHON].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_PYTHON].keywords[1]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_PYTHON, 0);
	set_sci_style(sci, SCE_P_DEFAULT, GEANY_FILETYPES_PYTHON, 0);
	set_sci_style(sci, SCE_P_COMMENTLINE, GEANY_FILETYPES_PYTHON, 1);
	set_sci_style(sci, SCE_P_NUMBER, GEANY_FILETYPES_PYTHON, 2);
	set_sci_style(sci, SCE_P_STRING, GEANY_FILETYPES_PYTHON, 3);
	set_sci_style(sci, SCE_P_CHARACTER, GEANY_FILETYPES_PYTHON, 4);
	set_sci_style(sci, SCE_P_WORD, GEANY_FILETYPES_PYTHON, 5);
	set_sci_style(sci, SCE_P_TRIPLE, GEANY_FILETYPES_PYTHON, 6);
	set_sci_style(sci, SCE_P_TRIPLEDOUBLE, GEANY_FILETYPES_PYTHON, 7);
	set_sci_style(sci, SCE_P_CLASSNAME, GEANY_FILETYPES_PYTHON, 8);
	set_sci_style(sci, SCE_P_DEFNAME, GEANY_FILETYPES_PYTHON, 9);
	set_sci_style(sci, SCE_P_OPERATOR, GEANY_FILETYPES_PYTHON, 10);
	set_sci_style(sci, SCE_P_IDENTIFIER, GEANY_FILETYPES_PYTHON, 11);
	set_sci_style(sci, SCE_P_COMMENTBLOCK, GEANY_FILETYPES_PYTHON, 12);
	set_sci_style(sci, SCE_P_STRINGEOL, GEANY_FILETYPES_PYTHON, 13);
	set_sci_style(sci, SCE_P_WORD2, GEANY_FILETYPES_PYTHON, 14);
	set_sci_style(sci, SCE_P_DECORATOR, GEANY_FILETYPES_PYTHON, 15);

	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.comment.python", (sptr_t) "1");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.quotes.python", (sptr_t) "1");
}


static void styleset_ruby_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_RUBY, 35);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[0]);
	get_keyfile_style(config, config_home, "commentline", &gsd_comment, &style_sets[GEANY_FILETYPES_RUBY].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x400080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "string", "0x008000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "character", "0x008000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_RUBY].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "global", "0x111199", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "symbol", "0x008020", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "classname", "0x7f0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_RUBY].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "defname", "0x7f0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "modulename", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_RUBY].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "backticks", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "instancevar", "0x000000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_RUBY].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "classvar", "0x000000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_RUBY].styling[15]);
	get_keyfile_hex(config, config_home, "styling", "datasection", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[16]);
	get_keyfile_hex(config, config_home, "styling", "heredelim", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[17]);
	get_keyfile_hex(config, config_home, "styling", "worddemoted", "0x111199", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[18]);
	get_keyfile_hex(config, config_home, "styling", "stdin", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[19]);
	get_keyfile_hex(config, config_home, "styling", "stdout", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[20]);
	get_keyfile_hex(config, config_home, "styling", "stderr", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[21]);
	get_keyfile_hex(config, config_home, "styling", "datasection", "0x600000", "0xfff0d8", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[22]);
	get_keyfile_hex(config, config_home, "styling", "regex", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[23]);
	get_keyfile_hex(config, config_home, "styling", "here_q", "0x7f007f", "0xddd0dd", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[24]);
	get_keyfile_hex(config, config_home, "styling", "here_qq", "0x7f007f", "0xddd0dd", "true", &style_sets[GEANY_FILETYPES_RUBY].styling[25]);
	get_keyfile_hex(config, config_home, "styling", "here_qx", "0x7f007f", "0xddd0dd", "true", &style_sets[GEANY_FILETYPES_RUBY].styling[26]);
	get_keyfile_hex(config, config_home, "styling", "string_q", "0x7f007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[27]);
	get_keyfile_hex(config, config_home, "styling", "string_qq", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[28]);
	get_keyfile_hex(config, config_home, "styling", "string_qx", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[29]);
	get_keyfile_hex(config, config_home, "styling", "string_qr", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[30]);
	get_keyfile_hex(config, config_home, "styling", "string_qw", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[31]);
	get_keyfile_hex(config, config_home, "styling", "upper_bound", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[32]);
	get_keyfile_hex(config, config_home, "styling", "error", "0xe500cc", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[33]);
	get_keyfile_hex(config, config_home, "styling", "pod", "0x035650", "0xffffff", "false", &style_sets[GEANY_FILETYPES_RUBY].styling[34]);

	style_sets[GEANY_FILETYPES_RUBY].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_RUBY, 0, "load define_method attr_accessor attr_writer attr_reader include __FILE__ and def end in or self unless __LINE__ begin defined? ensure module redo super until BEGIN break do false next rescue then when END case else for nil require retry true while alias class elsif if not return undef yield");
	style_sets[GEANY_FILETYPES_RUBY].keywords[1] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_RUBY].wordchars);
}


static void styleset_ruby(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_RUBY;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_RUBY, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_RUBY].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_RUBY, 0);
	set_sci_style(sci, SCE_RB_DEFAULT, GEANY_FILETYPES_RUBY, 0);
	set_sci_style(sci, SCE_RB_COMMENTLINE, GEANY_FILETYPES_RUBY, 1);
	set_sci_style(sci, SCE_RB_NUMBER, GEANY_FILETYPES_RUBY, 2);
	set_sci_style(sci, SCE_RB_STRING, GEANY_FILETYPES_RUBY, 3);
	set_sci_style(sci, SCE_RB_CHARACTER, GEANY_FILETYPES_RUBY, 4);
	set_sci_style(sci, SCE_RB_WORD, GEANY_FILETYPES_RUBY, 5);
	set_sci_style(sci, SCE_RB_GLOBAL, GEANY_FILETYPES_RUBY, 6);
	set_sci_style(sci, SCE_RB_SYMBOL, GEANY_FILETYPES_RUBY, 7);
	set_sci_style(sci, SCE_RB_CLASSNAME, GEANY_FILETYPES_RUBY, 8);
	set_sci_style(sci, SCE_RB_DEFNAME, GEANY_FILETYPES_RUBY, 9);
	set_sci_style(sci, SCE_RB_OPERATOR, GEANY_FILETYPES_RUBY, 10);
	set_sci_style(sci, SCE_RB_IDENTIFIER, GEANY_FILETYPES_RUBY, 11);
	set_sci_style(sci, SCE_RB_MODULE_NAME, GEANY_FILETYPES_RUBY, 12);
	set_sci_style(sci, SCE_RB_BACKTICKS, GEANY_FILETYPES_RUBY, 13);
	set_sci_style(sci, SCE_RB_INSTANCE_VAR, GEANY_FILETYPES_RUBY, 14);
	set_sci_style(sci, SCE_RB_CLASS_VAR, GEANY_FILETYPES_RUBY, 15);
	set_sci_style(sci, SCE_RB_DATASECTION, GEANY_FILETYPES_RUBY, 16);
	set_sci_style(sci, SCE_RB_HERE_DELIM, GEANY_FILETYPES_RUBY, 17);
	set_sci_style(sci, SCE_RB_WORD_DEMOTED, GEANY_FILETYPES_RUBY, 18);
	set_sci_style(sci, SCE_RB_STDIN, GEANY_FILETYPES_RUBY, 19);
	set_sci_style(sci, SCE_RB_STDOUT, GEANY_FILETYPES_RUBY, 20);
	set_sci_style(sci, SCE_RB_STDERR, GEANY_FILETYPES_RUBY, 21);
	set_sci_style(sci, SCE_RB_DATASECTION, GEANY_FILETYPES_RUBY, 22);
	set_sci_style(sci, SCE_RB_REGEX, GEANY_FILETYPES_RUBY, 23);
	set_sci_style(sci, SCE_RB_HERE_Q, GEANY_FILETYPES_RUBY, 24);
	set_sci_style(sci, SCE_RB_HERE_QQ, GEANY_FILETYPES_RUBY, 25);
	set_sci_style(sci, SCE_RB_HERE_QX, GEANY_FILETYPES_RUBY, 26);
	set_sci_style(sci, SCE_RB_STRING_Q, GEANY_FILETYPES_RUBY, 27);
	set_sci_style(sci, SCE_RB_STRING_QQ, GEANY_FILETYPES_RUBY, 28);
	set_sci_style(sci, SCE_RB_STRING_QX, GEANY_FILETYPES_RUBY, 29);
	set_sci_style(sci, SCE_RB_STRING_QR, GEANY_FILETYPES_RUBY, 30);
	set_sci_style(sci, SCE_RB_STRING_QW, GEANY_FILETYPES_RUBY, 31);
	set_sci_style(sci, SCE_RB_UPPER_BOUND, GEANY_FILETYPES_RUBY, 32);
	set_sci_style(sci, SCE_RB_ERROR, GEANY_FILETYPES_RUBY, 33);
	set_sci_style(sci, SCE_RB_POD, GEANY_FILETYPES_RUBY, 34);
}


static void styleset_sh_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_SH, 11);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SH].styling[0]);
	get_keyfile_style(config, config_home, "commentline", &gsd_comment, &style_sets[GEANY_FILETYPES_SH].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SH].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x119911", "0xffffff", "true", &style_sets[GEANY_FILETYPES_SH].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SH].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "character", "0x404000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SH].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SH].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SH].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "backticks", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_SH].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "param", "0x9f0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SH].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "scalar", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SH].styling[10]);

	style_sets[GEANY_FILETYPES_SH].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_SH, 0, "break case continue do done elif else esac eval exit export fi for goto if in integer return set shift then until while");
	style_sets[GEANY_FILETYPES_SH].keywords[1] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_SH].wordchars);
}


static void styleset_sh(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_SH;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_BASH, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_SH].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_SH, 0);
	set_sci_style(sci, SCE_SH_DEFAULT, GEANY_FILETYPES_SH, 0);
	set_sci_style(sci, SCE_SH_COMMENTLINE, GEANY_FILETYPES_SH, 1);
	set_sci_style(sci, SCE_SH_NUMBER, GEANY_FILETYPES_SH, 2);
	set_sci_style(sci, SCE_SH_WORD, GEANY_FILETYPES_SH, 3);
	set_sci_style(sci, SCE_SH_STRING, GEANY_FILETYPES_SH, 4);
	set_sci_style(sci, SCE_SH_CHARACTER, GEANY_FILETYPES_SH, 5);
	set_sci_style(sci, SCE_SH_OPERATOR, GEANY_FILETYPES_SH, 6);
	set_sci_style(sci, SCE_SH_IDENTIFIER, GEANY_FILETYPES_SH, 7);
	set_sci_style(sci, SCE_SH_BACKTICKS, GEANY_FILETYPES_SH, 8);
	set_sci_style(sci, SCE_SH_PARAM, GEANY_FILETYPES_SH, 9);
	set_sci_style(sci, SCE_SH_SCALAR, GEANY_FILETYPES_SH, 10);
}


static void styleset_xml(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_XML;

	styleset_common(sci, 7, ft_id);

	apply_filetype_properties(sci, SCLEX_XML, ft_id);

	/* use the same colouring for HTML; XML and so on */
	styleset_markup(sci, FALSE);
}


static void styleset_docbook_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_DOCBOOK, 29);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "tag", "0x000099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "tagunknown", "0xff0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "attribute", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "attributeunknown", "0xff0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x800080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "doublestring", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "singlestring", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "other", "0x800080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "entity", "0x800080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "tagend", "0x000099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "xmlstart", "0x000099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "xmlend", "0x000099", "0xf0f0f0", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "cdata", "0x660099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "question", "0x0000ff", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[15]);
	get_keyfile_hex(config, config_home, "styling", "value", "0x660099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[16]);
	get_keyfile_hex(config, config_home, "styling", "xccomment", "0x660099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[17]);
	get_keyfile_hex(config, config_home, "styling", "sgml_default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[18]);
	get_keyfile_hex(config, config_home, "styling", "sgml_comment", "0x303030", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[19]);
	get_keyfile_hex(config, config_home, "styling", "sgml_special", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[20]);
	get_keyfile_hex(config, config_home, "styling", "sgml_command", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[21]);
	get_keyfile_hex(config, config_home, "styling", "sgml_doublestring", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[22]);
	get_keyfile_hex(config, config_home, "styling", "sgml_simplestring", "0x404000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[23]);
	get_keyfile_hex(config, config_home, "styling", "sgml_1st_param", "0x404080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[24]);
	get_keyfile_hex(config, config_home, "styling", "sgml_entity", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[25]);
	get_keyfile_hex(config, config_home, "styling", "sgml_block_default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[26]);
	get_keyfile_hex(config, config_home, "styling", "sgml_1st_param_comment", "0x406090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[27]);
	get_keyfile_hex(config, config_home, "styling", "sgml_error", "0xff0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_DOCBOOK].styling[28]);

	style_sets[GEANY_FILETYPES_DOCBOOK].keywords = g_new(gchar*, 3);
	get_keyfile_keywords(config, config_home, "keywords", "elements", GEANY_FILETYPES_DOCBOOK, 0,
		   "abbrev abstract accel ackno acronym action address affiliation alt anchor \
			answer appendix appendixinfo application area areaset areaspec arg article \
			articleinfo artpagenums attribution audiodata audioobject author authorblurb \
			authorgroup authorinitials beginpage bibliocoverage bibliodiv biblioentry \
			bibliography bibliographyinfo biblioid bibliomisc bibliomixed bibliomset \
			bibliorelation biblioset bibliosource blockinfo blockquote book bookinfo \
			bridgehead callout calloutlist caption caution chapter chapterinfo citation \
			citebiblioid citerefentry citetitle city classname classsynopsis classsynopsisinfo \
			cmdsynopsis co collab collabname colophon nameend namest colname colspec command computeroutput \
			confdates confgroup confnum confsponsor conftitle constant constraint \
			constraintdef constructorsynopsis contractnum contractsponsor contrib \
			copyright coref corpauthor corpname country database date dedication \
			destructorsynopsis edition editor email emphasis entry entrytbl envar \
			epigraph equation errorcode errorname errortext errortype example \
			exceptionname fax fieldsynopsis figure filename fileref firstname firstterm \
			footnote footnoteref foreignphrase formalpara frame funcdef funcparams \
			funcprototype funcsynopsis funcsynopsisinfo function glossary glossaryinfo \
			glossdef glossdiv glossentry glosslist glosssee glossseealso glossterm \
			graphic graphicco group guibutton guiicon guilabel guimenu guimenuitem \
			guisubmenu hardware highlights holder honorific htm imagedata imageobject \
			imageobjectco important index indexdiv indexentry indexinfo indexterm \
			informalequation informalexample informalfigure informaltable initializer \
			inlineequation inlinegraphic inlinemediaobject interface interfacename \
			invpartnumber isbn issn issuenum itemizedlist itermset jobtitle keycap \
			keycode keycombo keysym keyword keywordset label legalnotice lhs lineage \
			lineannotation link listitem iteral literallayout lot lotentry manvolnum \
			markup medialabel mediaobject mediaobjectco member menuchoice methodname \
			methodparam methodsynopsis mm modespec modifier ousebutton msg msgaud \
			msgentry msgexplan msginfo msglevel msgmain msgorig msgrel msgset msgsub \
			msgtext nonterminal note objectinfo olink ooclass ooexception oointerface \
			option optional orderedlist orgdiv orgname otheraddr othercredit othername \
			pagenums para paramdef parameter part partinfo partintro personblurb \
			personname phone phrase pob postcode preface prefaceinfo primary primaryie \
			printhistory procedure production productionrecap productionset productname \
			productnumber programlisting programlistingco prompt property pubdate publisher \
			publishername pubsnumber qandadiv qandaentry qandaset question quote refclass \
			refdescriptor refentry refentryinfo refentrytitle reference referenceinfo \
			refmeta refmiscinfo refname refnamediv refpurpose refsect1 refsect1info refsect2 \
			refsect2info refsect3 refsect3info refsection refsectioninfo refsynopsisdiv \
			refsynopsisdivinfo releaseinfo remark replaceable returnvalue revdescription \
			revhistory revision revnumber revremark rhs row sbr screen screenco screeninfo \
			screenshot secondary secondaryie sect1 sect1info sect2 sect2info sect3 sect3info \
			sect4 sect4info sect5 sect5info section sectioninfo see seealso seealsoie \
			seeie seg seglistitem segmentedlist segtitle seriesvolnums set setindex \
			setindexinfo setinfo sgmltag shortaffil shortcut sidebar sidebarinfo simpara \
			simplelist simplemsgentry simplesect spanspec state step street structfield \
			structname subject subjectset subjectterm subscript substeps subtitle \
			superscript surname sv symbol synopfragment synopfragmentref synopsis \
			systemitem table tbody term tertiary tertiaryie textdata textobject tfoot \
			tgroup thead tip title titleabbrev toc tocback tocchap tocentry tocfront \
			toclevel1 toclevel2 toclevel3 toclevel4 toclevel5 tocpart token trademark \
			type ulink userinput varargs variablelist varlistentry varname videodata \
			videoobject void volumenum warning wordasword xref year cols colnum align spanname\
			arch condition conformance id lang os remap role revision revisionflag security \
			userlevel url vendor xreflabel status label endterm linkend space width");
	get_keyfile_keywords(config, config_home, "keywords", "dtd", GEANY_FILETYPES_DOCBOOK, 1, "ELEMENT DOCTYPE ATTLIST ENTITY NOTATION");
	style_sets[GEANY_FILETYPES_DOCBOOK].keywords[2] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_DOCBOOK].wordchars);
}


static void styleset_docbook(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_DOCBOOK;

	styleset_common(sci, 7, ft_id);

	apply_filetype_properties(sci, SCLEX_XML, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_DOCBOOK].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 5, (sptr_t) style_sets[GEANY_FILETYPES_DOCBOOK].keywords[1]);

	/* Unknown tags and attributes are highlighed in red.
	 * If a tag is actually OK, it should be added in lower case to the htmlKeyWords string. */

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_DOCBOOK, 0);
	set_sci_style(sci, SCE_H_DEFAULT, GEANY_FILETYPES_DOCBOOK, 0);
	set_sci_style(sci, SCE_H_TAG, GEANY_FILETYPES_DOCBOOK, 1);
	set_sci_style(sci, SCE_H_TAGUNKNOWN, GEANY_FILETYPES_DOCBOOK, 2);
	set_sci_style(sci, SCE_H_ATTRIBUTE, GEANY_FILETYPES_DOCBOOK, 3);
	set_sci_style(sci, SCE_H_ATTRIBUTEUNKNOWN, GEANY_FILETYPES_DOCBOOK, 4);
	set_sci_style(sci, SCE_H_NUMBER, GEANY_FILETYPES_DOCBOOK, 5);
	set_sci_style(sci, SCE_H_DOUBLESTRING, GEANY_FILETYPES_DOCBOOK, 6);
	set_sci_style(sci, SCE_H_SINGLESTRING, GEANY_FILETYPES_DOCBOOK, 7);
	set_sci_style(sci, SCE_H_OTHER, GEANY_FILETYPES_DOCBOOK, 8);
	set_sci_style(sci, SCE_H_COMMENT, GEANY_FILETYPES_DOCBOOK, 9);
	set_sci_style(sci, SCE_H_ENTITY, GEANY_FILETYPES_DOCBOOK, 10);
	set_sci_style(sci, SCE_H_TAGEND, GEANY_FILETYPES_DOCBOOK, 11);
	SSM(sci, SCI_STYLESETEOLFILLED, SCE_H_XMLSTART, 1);
	set_sci_style(sci, SCE_H_XMLSTART, GEANY_FILETYPES_DOCBOOK, 12);
	set_sci_style(sci, SCE_H_XMLEND, GEANY_FILETYPES_DOCBOOK, 13);
	set_sci_style(sci, SCE_H_CDATA, GEANY_FILETYPES_DOCBOOK, 14);
	set_sci_style(sci, SCE_H_QUESTION, GEANY_FILETYPES_DOCBOOK, 15);
	set_sci_style(sci, SCE_H_VALUE, GEANY_FILETYPES_DOCBOOK, 16);
	set_sci_style(sci, SCE_H_XCCOMMENT, GEANY_FILETYPES_DOCBOOK, 17);
	set_sci_style(sci, SCE_H_SGML_DEFAULT, GEANY_FILETYPES_DOCBOOK, 18);
	set_sci_style(sci, SCE_H_DEFAULT, GEANY_FILETYPES_DOCBOOK, 19);
	set_sci_style(sci, SCE_H_SGML_SPECIAL, GEANY_FILETYPES_DOCBOOK, 20);
	set_sci_style(sci, SCE_H_SGML_COMMAND, GEANY_FILETYPES_DOCBOOK, 21);
	set_sci_style(sci, SCE_H_SGML_DOUBLESTRING, GEANY_FILETYPES_DOCBOOK, 22);
	set_sci_style(sci, SCE_H_SGML_SIMPLESTRING, GEANY_FILETYPES_DOCBOOK, 23);
	set_sci_style(sci, SCE_H_SGML_1ST_PARAM, GEANY_FILETYPES_DOCBOOK, 24);
	set_sci_style(sci, SCE_H_SGML_ENTITY, GEANY_FILETYPES_DOCBOOK, 25);
	set_sci_style(sci, SCE_H_SGML_BLOCK_DEFAULT, GEANY_FILETYPES_DOCBOOK, 26);
	set_sci_style(sci, SCE_H_SGML_1ST_PARAM_COMMENT, GEANY_FILETYPES_DOCBOOK, 27);
	set_sci_style(sci, SCE_H_SGML_ERROR, GEANY_FILETYPES_DOCBOOK, 28);

	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.html", (sptr_t) "1");
	SSM(sci, SCI_SETPROPERTY, (sptr_t) "fold.html.preprocessor", (sptr_t) "1");
}


static void styleset_none(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_NONE;

	SSM(sci, SCI_SETLEXER, SCLEX_NULL, 0);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_NONE, GCS_DEFAULT);

	styleset_common(sci, 5, ft_id);

	SSM(sci, SCI_SETWORDCHARS, 0, (sptr_t) common_style_set.wordchars);
	SSM(sci, SCI_SETWHITESPACECHARS, 0, (sptr_t) whitespace_chars);
}


static void styleset_css_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_CSS, 16);
	get_keyfile_hex(config, config_home, "styling", "default", "0x003399", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "tag", "0x2166a4", "0xffffff", "true", &style_sets[GEANY_FILETYPES_CSS].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "class", "0x007f00", "0xffffff", "true", &style_sets[GEANY_FILETYPES_CSS].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "pseudoclass", "0x660010", "0xffffff", "true", &style_sets[GEANY_FILETYPES_CSS].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "unknown_pseudoclass", "0xff0099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "unknown_identifier", "0xff0099", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000099", "0xffffff", "true", &style_sets[GEANY_FILETYPES_CSS].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "doublestring", "0x330066", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "singlestring", "0x330066", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "attribute", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "value", "0x303030", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "id", "0x7f0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "identifier2", "0x6b6bff", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CSS].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "important", "0xff0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_CSS].styling[15]);

	style_sets[GEANY_FILETYPES_CSS].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_CSS, 0,
								"color background-color background-image background-repeat background-attachment background-position background \
								font-family font-style font-variant font-weight font-size font \
								word-spacing letter-spacing text-decoration vertical-align text-transform text-align text-indent line-height \
								margin-top margin-right margin-bottom margin-left margin \
								padding-top padding-right padding-bottom padding-left padding \
								border-top-width border-right-width border-bottom-width border-left-width border-width \
								border-top border-right border-bottom border-left border \
								border-color border-style width height float clear \
								display white-space list-style-type list-style-image list-style-position list-style");
	get_keyfile_keywords(config, config_home, "keywords", "pseudoclasses", GEANY_FILETYPES_CSS, 1, "first-letter first-line link active visited lang first-child focus hover before after left right first");
	get_keyfile_keywords(config, config_home, "keywords", "secondary", GEANY_FILETYPES_CSS, 2,
								"border-top-color border-right-color border-bottom-color border-left-color border-color \
								border-top-style border-right-style border-bottom-style border-left-style border-style \
								top right bottom left position z-index direction unicode-bidi \
								min-width max-width min-height max-height overflow clip visibility content quotes \
								counter-reset counter-increment marker-offset \
								size marks page-break-before page-break-after page-break-inside page orphans widows \
								font-stretch font-size-adjust unicode-range units-per-em src \
								panose-1 stemv stemh slope cap-height x-height ascent descent widths bbox definition-src \
								baseline centerline mathline topline text-shadow \
								caption-side table-layout border-collapse border-spacing empty-cells speak-header \
								cursor outline outline-width outline-style outline-color \
								volume speak pause-before pause-after pause cue-before cue-after cue \
								play-during azimuth elevation speech-rate voice-family pitch pitch-range stress richness \
								speak-punctuation speak-numeral");
	style_sets[GEANY_FILETYPES_CSS].keywords[3] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_CSS].wordchars);
}


static void styleset_css(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_CSS;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_CSS, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_CSS].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_CSS].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_CSS].keywords[2]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_CSS, 0);
	set_sci_style(sci, SCE_CSS_DEFAULT, GEANY_FILETYPES_CSS, 0);
	set_sci_style(sci, SCE_CSS_COMMENT, GEANY_FILETYPES_CSS, 1);
	set_sci_style(sci, SCE_CSS_TAG, GEANY_FILETYPES_CSS, 2);
	set_sci_style(sci, SCE_CSS_CLASS, GEANY_FILETYPES_CSS, 3);
	set_sci_style(sci, SCE_CSS_PSEUDOCLASS, GEANY_FILETYPES_CSS, 4);
	set_sci_style(sci, SCE_CSS_UNKNOWN_PSEUDOCLASS, GEANY_FILETYPES_CSS, 5);
	set_sci_style(sci, SCE_CSS_UNKNOWN_IDENTIFIER, GEANY_FILETYPES_CSS, 6);
	set_sci_style(sci, SCE_CSS_OPERATOR, GEANY_FILETYPES_CSS, 7);
	set_sci_style(sci, SCE_CSS_IDENTIFIER, GEANY_FILETYPES_CSS, 8);
	set_sci_style(sci, SCE_CSS_DOUBLESTRING, GEANY_FILETYPES_CSS, 9);
	set_sci_style(sci, SCE_CSS_SINGLESTRING, GEANY_FILETYPES_CSS, 10);
	set_sci_style(sci, SCE_CSS_ATTRIBUTE, GEANY_FILETYPES_CSS, 11);
	set_sci_style(sci, SCE_CSS_VALUE, GEANY_FILETYPES_CSS, 12);
	set_sci_style(sci, SCE_CSS_ID, GEANY_FILETYPES_CSS, 13);
	set_sci_style(sci, SCE_CSS_IDENTIFIER2, GEANY_FILETYPES_CSS, 14);
	set_sci_style(sci, SCE_CSS_IMPORTANT, GEANY_FILETYPES_CSS, 15);
}


static void styleset_conf_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_CONF, 6);
	get_keyfile_hex(config, config_home, "styling", "default", "0x7f0000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CONF].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CONF].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "section", "0x000090", "0xffffff", "true", &style_sets[GEANY_FILETYPES_CONF].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "key", "0x00007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CONF].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "assignment", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CONF].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "defval", "0x00007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CONF].styling[5]);

	style_sets[GEANY_FILETYPES_CONF].keywords = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_CONF].wordchars);
}


static void styleset_conf(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_CONF;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_PROPERTIES, ft_id);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_CONF, 0);
	set_sci_style(sci, SCE_PROPS_DEFAULT, GEANY_FILETYPES_CONF, 0);
	set_sci_style(sci, SCE_PROPS_COMMENT, GEANY_FILETYPES_CONF, 1);
	set_sci_style(sci, SCE_PROPS_SECTION, GEANY_FILETYPES_CONF, 2);
	set_sci_style(sci, SCE_PROPS_KEY, GEANY_FILETYPES_CONF, 3);
	set_sci_style(sci, SCE_PROPS_ASSIGNMENT, GEANY_FILETYPES_CONF, 4);
	set_sci_style(sci, SCE_PROPS_DEFVAL, GEANY_FILETYPES_CONF, 5);
}


static void styleset_asm_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_ASM, 15);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x880000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "cpuinstruction", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_ASM].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "mathinstruction", "0x7f0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_ASM].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "register", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "directive", "0x3d670f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_ASM].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "directiveoperand", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "commentblock", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "character", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "stringeol", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_ASM].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "extinstruction", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_ASM].styling[14]);

	style_sets[GEANY_FILETYPES_ASM].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "keywords", "instructions", GEANY_FILETYPES_ASM, 0, "HLT LAD SPI ADD SUB MUL DIV JMP JEZ JGZ JLZ SWAP JSR RET PUSHAC POPAC ADDST SUBST MULST DIVST LSA LDS PUSH POP CLI LDI INK LIA DEK LDX");
	get_keyfile_keywords(config, config_home, "keywords", "registers", GEANY_FILETYPES_ASM, 1, "");
	get_keyfile_keywords(config, config_home, "keywords", "directives", GEANY_FILETYPES_ASM, 2, "ORG LIST NOLIST PAGE EQUIVALENT WORD TEXT");
	style_sets[GEANY_FILETYPES_ASM].keywords[3] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_ASM].wordchars);
}


static void styleset_asm(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_ASM;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_ASM, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_ASM].keywords[0]);
	/*SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_ASM].keywords[0]);*/
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_ASM].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 3, (sptr_t) style_sets[GEANY_FILETYPES_ASM].keywords[2]);
	/*SSM(sci, SCI_SETKEYWORDS, 5, (sptr_t) style_sets[GEANY_FILETYPES_ASM].keywords[0]);*/

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_ASM, 0);
	set_sci_style(sci, SCE_ASM_DEFAULT, GEANY_FILETYPES_ASM, 0);
	set_sci_style(sci, SCE_ASM_COMMENT, GEANY_FILETYPES_ASM, 1);
	set_sci_style(sci, SCE_ASM_NUMBER, GEANY_FILETYPES_ASM, 2);
	set_sci_style(sci, SCE_ASM_STRING, GEANY_FILETYPES_ASM, 3);
	set_sci_style(sci, SCE_ASM_OPERATOR, GEANY_FILETYPES_ASM, 4);
	set_sci_style(sci, SCE_ASM_IDENTIFIER, GEANY_FILETYPES_ASM, 5);
	set_sci_style(sci, SCE_ASM_CPUINSTRUCTION, GEANY_FILETYPES_ASM, 6);
	set_sci_style(sci, SCE_ASM_MATHINSTRUCTION, GEANY_FILETYPES_ASM, 7);
	set_sci_style(sci, SCE_ASM_REGISTER, GEANY_FILETYPES_ASM, 8);
	set_sci_style(sci, SCE_ASM_DIRECTIVE, GEANY_FILETYPES_ASM, 9);
	set_sci_style(sci, SCE_ASM_DIRECTIVEOPERAND, GEANY_FILETYPES_ASM, 10);
	set_sci_style(sci, SCE_ASM_COMMENTBLOCK, GEANY_FILETYPES_ASM, 11);
	set_sci_style(sci, SCE_ASM_CHARACTER, GEANY_FILETYPES_ASM, 12);
	set_sci_style(sci, SCE_ASM_STRINGEOL, GEANY_FILETYPES_ASM, 13);
	set_sci_style(sci, SCE_ASM_EXTINSTRUCTION, GEANY_FILETYPES_ASM, 14);
}


static void styleset_fortran_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_FORTRAN, 15);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_FORTRAN].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_FORTRAN].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_FORTRAN].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_FORTRAN].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_FORTRAN].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_FORTRAN].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "string2", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_FORTRAN].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x7f0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_FORTRAN].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "word2", "0x000099", "0xffffff", "true", &style_sets[GEANY_FILETYPES_FORTRAN].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "word3", "0x3d670f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_FORTRAN].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "preprocessor", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_FORTRAN].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "operator2", "0x301010", "0xffffff", "true", &style_sets[GEANY_FILETYPES_FORTRAN].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "continuation", "0x000000", "0xf0e080", "false", &style_sets[GEANY_FILETYPES_FORTRAN].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "stringeol", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_FORTRAN].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "label", "0xa861a8", "0xffffff", "true", &style_sets[GEANY_FILETYPES_FORTRAN].styling[14]);

	style_sets[GEANY_FILETYPES_FORTRAN].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_FORTRAN, 0, "");
	get_keyfile_keywords(config, config_home, "keywords", "intrinsic_functions", GEANY_FILETYPES_FORTRAN, 1, "");
	get_keyfile_keywords(config, config_home, "keywords", "user_functions", GEANY_FILETYPES_FORTRAN, 2, "");
	style_sets[GEANY_FILETYPES_FORTRAN].keywords[3] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_FORTRAN].wordchars);
}


static void styleset_fortran(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_FORTRAN;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_F77, ft_id);	/* SCLEX_FORTRAN */

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_FORTRAN].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_FORTRAN].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_FORTRAN].keywords[2]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_FORTRAN, 0);
	set_sci_style(sci, SCE_F_DEFAULT, GEANY_FILETYPES_FORTRAN, 0);
	set_sci_style(sci, SCE_F_COMMENT, GEANY_FILETYPES_FORTRAN, 1);
	set_sci_style(sci, SCE_F_NUMBER, GEANY_FILETYPES_FORTRAN, 2);
	set_sci_style(sci, SCE_F_STRING1, GEANY_FILETYPES_FORTRAN, 3);
	set_sci_style(sci, SCE_F_OPERATOR, GEANY_FILETYPES_FORTRAN, 4);
	set_sci_style(sci, SCE_F_IDENTIFIER, GEANY_FILETYPES_FORTRAN, 5);
	set_sci_style(sci, SCE_F_STRING2, GEANY_FILETYPES_FORTRAN, 6);
	set_sci_style(sci, SCE_F_WORD, GEANY_FILETYPES_FORTRAN, 7);
	set_sci_style(sci, SCE_F_WORD2, GEANY_FILETYPES_FORTRAN, 8);
	set_sci_style(sci, SCE_F_WORD3, GEANY_FILETYPES_FORTRAN, 9);
	set_sci_style(sci, SCE_F_PREPROCESSOR, GEANY_FILETYPES_FORTRAN, 10);
	set_sci_style(sci, SCE_F_OPERATOR2, GEANY_FILETYPES_FORTRAN, 11);
	set_sci_style(sci, SCE_F_CONTINUATION, GEANY_FILETYPES_FORTRAN, 12);
	set_sci_style(sci, SCE_F_STRINGEOL, GEANY_FILETYPES_FORTRAN, 13);
	set_sci_style(sci, SCE_F_LABEL, GEANY_FILETYPES_FORTRAN, 14);
}


static void styleset_sql_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_SQL, 15);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "commentline", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "commentdoc", "0x3f5fbf", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x7f7f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x001a7f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_SQL].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "word2", "0x7f0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_SQL].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "string", "0x7f007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "character", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x000000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_SQL].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x111199", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "sqlplus", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "sqlplus_prompt", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "sqlplus_comment", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "quotedidentifier", "0x111199", "0xffffff", "false", &style_sets[GEANY_FILETYPES_SQL].styling[14]);

	style_sets[GEANY_FILETYPES_SQL].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", "keywords", GEANY_FILETYPES_SQL, 0,
						"absolute action add admin after aggregate \
						alias all allocate alter and any are array as asc \
						assertion at authorization before begin binary bit blob boolean both breadth by \
						call cascade cascaded case cast catalog char character check class clob close collate \
						collation column commit completion connect connection constraint constraints \
						constructor continue corresponding create cross cube current \
						current_date current_path current_role current_time current_timestamp \
						current_user cursor cycle data date day deallocate dec decimal declare default \
						deferrable deferred delete depth deref desc describe descriptor destroy destructor \
						deterministic dictionary diagnostics disconnect distinct domain double drop dynamic \
						each else end end-exec equals escape every except exception exec execute external \
						false fetch first float for foreign found from free full function general get global \
						go goto grant group grouping having host hour identity if ignore immediate in indicator \
						initialize initially inner inout input insert int integer intersect interval \
						into is isolation iterate join key language large last lateral leading left less level like \
						limit local localtime localtimestamp locator map match minute modifies modify module month \
						names national natural nchar nclob new next no none not null numeric object of off old on only \
						open operation option or order ordinality out outer output pad parameter parameters partial path \
						postfix precision prefix preorder prepare preserve primary prior privileges procedure public \
						read reads real recursive ref references referencing relative restrict result return returns \
						revoke right role rollback rollup routine row rows savepoint schema scroll scope search \
						second section select sequence session session_user set sets size smallint some space \
						specific specifictype sql sqlexception sqlstate sqlwarning start state statement static \
						structure system_user table temporary terminate than then time timestamp \
						timezone_hour timezone_minute to trailing transaction translation year zone\
						treat trigger true under union unique unknown unnest update usage user using \
						value values varchar variable varying view when whenever where with without work write");
	style_sets[GEANY_FILETYPES_SQL].keywords[1] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_SQL].wordchars);
}


static void styleset_sql(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_SQL;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_SQL, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_SQL].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_SQL, 0);
	set_sci_style(sci, SCE_SQL_DEFAULT, GEANY_FILETYPES_SQL, 0);
	set_sci_style(sci, SCE_SQL_COMMENT, GEANY_FILETYPES_SQL, 1);
	set_sci_style(sci, SCE_SQL_COMMENTLINE, GEANY_FILETYPES_SQL, 2);
	set_sci_style(sci, SCE_SQL_COMMENTDOC, GEANY_FILETYPES_SQL, 3);
	set_sci_style(sci, SCE_SQL_NUMBER, GEANY_FILETYPES_SQL, 4);
	set_sci_style(sci, SCE_SQL_WORD, GEANY_FILETYPES_SQL, 5);
	set_sci_style(sci, SCE_SQL_WORD2, GEANY_FILETYPES_SQL, 6);
	set_sci_style(sci, SCE_SQL_STRING, GEANY_FILETYPES_SQL, 7);
	set_sci_style(sci, SCE_SQL_CHARACTER, GEANY_FILETYPES_SQL, 8);
	set_sci_style(sci, SCE_SQL_OPERATOR, GEANY_FILETYPES_SQL, 9);
	set_sci_style(sci, SCE_SQL_IDENTIFIER, GEANY_FILETYPES_SQL, 10);
	set_sci_style(sci, SCE_SQL_SQLPLUS, GEANY_FILETYPES_SQL, 11);
	set_sci_style(sci, SCE_SQL_SQLPLUS_PROMPT, GEANY_FILETYPES_SQL, 12);
	set_sci_style(sci, SCE_SQL_SQLPLUS_COMMENT, GEANY_FILETYPES_SQL, 13);
	set_sci_style(sci, SCE_SQL_QUOTEDIDENTIFIER, GEANY_FILETYPES_SQL, 14);
}


static void styleset_haskell_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_HASKELL, 17);

	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "commentline", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "commentblock", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "commentblock2", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "commentblock3", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "keyword", "0x00007f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_HASKELL].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "import", "0x991111", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "character", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "class", "0x0000d0", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "instance", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "capital", "0x635b00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "module", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[15]);
	get_keyfile_hex(config, config_home, "styling", "data", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_HASKELL].styling[16]);

	style_sets[GEANY_FILETYPES_HASKELL].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", "keywords", GEANY_FILETYPES_HASKELL, 0,
			"as case class data deriving do else if import in infixl infixr instance let module of primitive qualified then type where");
	style_sets[GEANY_FILETYPES_HASKELL].keywords[1] = NULL;

	get_keyfile_wordchars(config, config_home, &style_sets[GEANY_FILETYPES_HASKELL].wordchars);
}


static void styleset_haskell(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_HASKELL;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_HASKELL, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_HASKELL].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_HASKELL, 0);
	set_sci_style(sci, SCE_HA_DEFAULT, GEANY_FILETYPES_HASKELL, 0);
	set_sci_style(sci, SCE_HA_COMMENTLINE, GEANY_FILETYPES_HASKELL, 1);
	set_sci_style(sci, SCE_HA_COMMENTBLOCK, GEANY_FILETYPES_HASKELL, 2);
	set_sci_style(sci, SCE_HA_COMMENTBLOCK2, GEANY_FILETYPES_HASKELL, 3);
	set_sci_style(sci, SCE_HA_COMMENTBLOCK3, GEANY_FILETYPES_HASKELL, 4);
	set_sci_style(sci, SCE_HA_NUMBER, GEANY_FILETYPES_HASKELL, 5);
	set_sci_style(sci, SCE_HA_KEYWORD, GEANY_FILETYPES_HASKELL, 6);
	set_sci_style(sci, SCE_HA_IMPORT, GEANY_FILETYPES_HASKELL, 7);
	set_sci_style(sci, SCE_HA_STRING, GEANY_FILETYPES_HASKELL, 8);
	set_sci_style(sci, SCE_HA_CHARACTER, GEANY_FILETYPES_HASKELL, 9);
	set_sci_style(sci, SCE_HA_CLASS, GEANY_FILETYPES_HASKELL, 10);
	set_sci_style(sci, SCE_HA_OPERATOR, GEANY_FILETYPES_HASKELL, 11);
	set_sci_style(sci, SCE_HA_IDENTIFIER, GEANY_FILETYPES_HASKELL, 12);
	set_sci_style(sci, SCE_HA_INSTANCE, GEANY_FILETYPES_HASKELL, 13);
	set_sci_style(sci, SCE_HA_CAPITAL, GEANY_FILETYPES_HASKELL, 14);
	set_sci_style(sci, SCE_HA_MODULE, GEANY_FILETYPES_HASKELL, 15);
	set_sci_style(sci, SCE_HA_DATA, GEANY_FILETYPES_HASKELL, 16);
}


static void styleset_caml_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_CAML, 14);

	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "comment1", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "comment2", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "comment3", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x7f7f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "keyword", "0x001a7f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_CAML].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "keyword2", "0x7f0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_CAML].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "string", "0x7f007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "char", "0x7f007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x111199", "0xffffff", "false", &style_sets[GEANY_FILETYPES_CAML].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "tagname", "0x000000", "0xffe0ff", "true", &style_sets[GEANY_FILETYPES_CAML].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "linenum", "0x000000", "0xc0c0c0", "false", &style_sets[GEANY_FILETYPES_CAML].styling[13]);

	style_sets[GEANY_FILETYPES_CAML].keywords = g_new(gchar*, 3);
	get_keyfile_keywords(config, config_home, "keywords", "keywords", GEANY_FILETYPES_CAML, 0,
			"and as assert asr begin class constraint do \
			done downto else end exception external false for fun function functor if in include inherit \
			initializer land lazy let lor lsl lsr lxor match method mod module mutable new object of open \
			or private rec sig struct then to true try type val virtual when while with");
	get_keyfile_keywords(config, config_home, "keywords", "keywords_optional", GEANY_FILETYPES_CAML, 1, "option Some None ignore ref");
	style_sets[GEANY_FILETYPES_CAML].keywords[2] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_CAML].wordchars);
}


static void styleset_caml(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_CAML;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_CAML, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_CAML].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_CAML].keywords[1]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_CAML, 0);
	set_sci_style(sci, SCE_CAML_DEFAULT, GEANY_FILETYPES_CAML, 0);
	set_sci_style(sci, SCE_CAML_COMMENT, GEANY_FILETYPES_CAML, 1);
	set_sci_style(sci, SCE_CAML_COMMENT1, GEANY_FILETYPES_CAML, 2);
	set_sci_style(sci, SCE_CAML_COMMENT2, GEANY_FILETYPES_CAML, 3);
	set_sci_style(sci, SCE_CAML_COMMENT3, GEANY_FILETYPES_CAML, 4);
	set_sci_style(sci, SCE_CAML_NUMBER, GEANY_FILETYPES_CAML, 5);
	set_sci_style(sci, SCE_CAML_KEYWORD, GEANY_FILETYPES_CAML, 6);
	set_sci_style(sci, SCE_CAML_KEYWORD2, GEANY_FILETYPES_CAML, 7);
	set_sci_style(sci, SCE_CAML_STRING, GEANY_FILETYPES_CAML, 8);
	set_sci_style(sci, SCE_CAML_CHAR, GEANY_FILETYPES_CAML, 9);
	set_sci_style(sci, SCE_CAML_OPERATOR, GEANY_FILETYPES_CAML, 10);
	set_sci_style(sci, SCE_CAML_IDENTIFIER, GEANY_FILETYPES_CAML, 11);
	set_sci_style(sci, SCE_CAML_TAGNAME, GEANY_FILETYPES_CAML, 12);
	set_sci_style(sci, SCE_CAML_LINENUM, GEANY_FILETYPES_CAML, 13);
}




static void styleset_oms_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_OMS, 11);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "commentline", "0x909090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x991111", "0xffffff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "character", "0x404000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "backticks", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_OMS].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "param", "0x991111", "0x0000ff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "scalar", "0x0000ff", "0xffffff", "false", &style_sets[GEANY_FILETYPES_OMS].styling[10]);

	style_sets[GEANY_FILETYPES_OMS].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_OMS, 0, "clear seq fillcols fillrowsgaspect gaddview \
			gtitle gxaxis gyaxis max contour gcolor gplot gaddview gxaxis gyaxis gcolor fill coldim gplot \
			gtitle clear arcov dpss fspec cos gxaxis gyaxis gtitle gplot gupdate rowdim fill print for to begin \
			end write cocreate coinvoke codispsave cocreate codispset copropput colsum sqrt adddialog \
			addcontrol addcontrol delwin fillrows function gaspect conjdir");
	style_sets[GEANY_FILETYPES_OMS].keywords[1] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_OMS].wordchars);
}


static void styleset_oms(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_OMS;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_OMS, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_OMS].keywords[0]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_OMS, 0);
	set_sci_style(sci, SCE_SH_DEFAULT, GEANY_FILETYPES_OMS, 0);
	set_sci_style(sci, SCE_SH_COMMENTLINE, GEANY_FILETYPES_OMS, 1);
	set_sci_style(sci, SCE_SH_NUMBER, GEANY_FILETYPES_OMS, 2);
	set_sci_style(sci, SCE_SH_WORD, GEANY_FILETYPES_OMS, 3);
	set_sci_style(sci, SCE_SH_STRING, GEANY_FILETYPES_OMS, 4);
	set_sci_style(sci, SCE_SH_CHARACTER, GEANY_FILETYPES_OMS, 5);
	set_sci_style(sci, SCE_SH_OPERATOR, GEANY_FILETYPES_OMS, 6);
	set_sci_style(sci, SCE_SH_IDENTIFIER, GEANY_FILETYPES_OMS, 7);
	set_sci_style(sci, SCE_SH_BACKTICKS, GEANY_FILETYPES_OMS, 8);
	set_sci_style(sci, SCE_SH_PARAM, GEANY_FILETYPES_OMS, 9);
	set_sci_style(sci, SCE_SH_SCALAR, GEANY_FILETYPES_OMS, 10);
}


static void styleset_tcl_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_TCL, 16);
	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_TCL].styling[0]);
	get_keyfile_style(config, config_home, "comment", &gsd_comment, &style_sets[GEANY_FILETYPES_TCL].styling[1]);
	get_keyfile_style(config, config_home, "commentline", &gsd_comment, &style_sets[GEANY_FILETYPES_TCL].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_TCL].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_TCL].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0xa20000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_TCL].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "wordinquote", "0x7f007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_TCL].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "inquote", "0x7f007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_TCL].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "substitution", "0x111199", "0xffffff", "false", &style_sets[GEANY_FILETYPES_TCL].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "modifier", "0x7f007f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_TCL].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "expand", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_TCL].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "wordtcl", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_TCL].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "wordtk", "0x7f0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_TCL].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "worditcl", "0x111199", "0xffffff", "true", &style_sets[GEANY_FILETYPES_TCL].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "wordtkcmds", "0x7f0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_TCL].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "wordexpand", "0x7f0000", "0xffffff", "true", &style_sets[GEANY_FILETYPES_TCL].styling[15]);

	style_sets[GEANY_FILETYPES_TCL].keywords = g_new(gchar*, 6);
	get_keyfile_keywords(config, config_home, "keywords", "tcl", GEANY_FILETYPES_TCL, 0, "");
	get_keyfile_keywords(config, config_home, "keywords", "tk", GEANY_FILETYPES_TCL, 1, "");
	get_keyfile_keywords(config, config_home, "keywords", "itcl", GEANY_FILETYPES_TCL, 2, "");
	get_keyfile_keywords(config, config_home, "keywords", "tkcommands", GEANY_FILETYPES_TCL, 3, "");
	get_keyfile_keywords(config, config_home, "keywords", "expand", GEANY_FILETYPES_TCL, 4, "");
	style_sets[GEANY_FILETYPES_TCL].keywords[5] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_TCL].wordchars);
}


static void styleset_tcl(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_TCL;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_TCL, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_TCL].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_TCL].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_TCL].keywords[2]);
	SSM(sci, SCI_SETKEYWORDS, 3, (sptr_t) style_sets[GEANY_FILETYPES_TCL].keywords[3]);
	SSM(sci, SCI_SETKEYWORDS, 4, (sptr_t) style_sets[GEANY_FILETYPES_TCL].keywords[4]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_TCL, 0);
	set_sci_style(sci, SCE_TCL_DEFAULT, GEANY_FILETYPES_TCL, 0);
	set_sci_style(sci, SCE_TCL_COMMENT, GEANY_FILETYPES_TCL, 1);
	set_sci_style(sci, SCE_TCL_COMMENTLINE, GEANY_FILETYPES_TCL, 2);
	set_sci_style(sci, SCE_TCL_NUMBER, GEANY_FILETYPES_TCL, 3);
	set_sci_style(sci, SCE_TCL_OPERATOR, GEANY_FILETYPES_TCL, 4);
	set_sci_style(sci, SCE_TCL_IDENTIFIER, GEANY_FILETYPES_TCL, 5);
	set_sci_style(sci, SCE_TCL_WORD_IN_QUOTE, GEANY_FILETYPES_TCL, 6);
	set_sci_style(sci, SCE_TCL_IN_QUOTE, GEANY_FILETYPES_TCL, 7);
	set_sci_style(sci, SCE_TCL_SUBSTITUTION, GEANY_FILETYPES_TCL, 8);
	set_sci_style(sci, SCE_TCL_MODIFIER, GEANY_FILETYPES_TCL, 9);
	set_sci_style(sci, SCE_TCL_EXPAND, GEANY_FILETYPES_TCL, 10);
	set_sci_style(sci, SCE_TCL_WORD, GEANY_FILETYPES_TCL, 11);
	set_sci_style(sci, SCE_TCL_WORD2, GEANY_FILETYPES_TCL, 12);
	set_sci_style(sci, SCE_TCL_WORD3, GEANY_FILETYPES_TCL, 13);
	set_sci_style(sci, SCE_TCL_WORD4, GEANY_FILETYPES_TCL, 14);
	set_sci_style(sci, SCE_TCL_WORD5, GEANY_FILETYPES_TCL, 15);
}

static void styleset_d_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_D, 18);

	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0xd00000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "commentline", "0xd00000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "commentdoc", "0x3f5fbf", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "commentdocnested", "0x3f5fbf", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x00007f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_D].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "word2", "0x991111", "0xffffff", "true", &style_sets[GEANY_FILETYPES_D].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "word3", "0x991111", "0xffffff", "true", &style_sets[GEANY_FILETYPES_D].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "typedef", "0x0000d0", "0xffffff", "true", &style_sets[GEANY_FILETYPES_D].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "stringeol", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_D].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "character", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "commentlinedoc", "0x3f5fbf", "0xffffff", "true", &style_sets[GEANY_FILETYPES_D].styling[15]);
	get_keyfile_hex(config, config_home, "styling", "commentdockeyword", "0x3f5fbf", "0xffffff", "true", &style_sets[GEANY_FILETYPES_D].styling[16]);
	get_keyfile_hex(config, config_home, "styling", "commentdockeyworderror", "0x3f5fbf", "0xffffff", "false", &style_sets[GEANY_FILETYPES_D].styling[17]);

	style_sets[GEANY_FILETYPES_D].keywords = g_new(gchar*, 5);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_D, 0,
			"__FILE__ __LINE__ __DATA__ __TIME__ __TIMESTAMP__ abstract alias align asm assert auto \
			 body bool break byte case cast catch cdouble cent cfloat char class const continue creal \
			 dchar debug default delegate delete deprecated do double else enum export extern false \
			 final finally float for foreach function goto idouble if ifloat import in inout int \
			 interface invariant ireal is long mixin module new null out override package pragma \
			 private protected public real return scope short static struct super switch \
			 synchronized template this throw true try typedef typeof ubyte ucent uint ulong union \
			 unittest ushort version void volatile wchar while with");
	get_keyfile_keywords(config, config_home, "keywords", "secondary", GEANY_FILETYPES_D, 1,
			"");
	get_keyfile_keywords(config, config_home, "keywords", "docComment", GEANY_FILETYPES_D, 2,
			"Authors Bugs Copyright Date Deprecated Examples History License Macros Params Returns \
			 See_Also Standards Throws Version");
	get_keyfile_keywords(config, config_home, "keywords", "types", GEANY_FILETYPES_D, 3,
			"");
	style_sets[GEANY_FILETYPES_D].keywords[4] = NULL;

	get_keyfile_wordchars(config, config_home, &style_sets[GEANY_FILETYPES_D].wordchars);
}


static void styleset_d(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_D;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_D, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_D].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_D].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_D].keywords[2]);
	SSM(sci, SCI_SETKEYWORDS, 3, (sptr_t) style_sets[GEANY_FILETYPES_D].keywords[3]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_D, 0);
	set_sci_style(sci, SCE_D_DEFAULT, GEANY_FILETYPES_D, 0);
	set_sci_style(sci, SCE_D_COMMENT, GEANY_FILETYPES_D, 1);
	set_sci_style(sci, SCE_D_COMMENTLINE, GEANY_FILETYPES_D, 2);
	set_sci_style(sci, SCE_D_COMMENTDOC, GEANY_FILETYPES_D, 3);
	set_sci_style(sci, SCE_D_COMMENTNESTED, GEANY_FILETYPES_D, 4);
	set_sci_style(sci, SCE_D_NUMBER, GEANY_FILETYPES_D, 5);
	set_sci_style(sci, SCE_D_WORD, GEANY_FILETYPES_D, 6);
	set_sci_style(sci, SCE_D_WORD2, GEANY_FILETYPES_D, 7);
	set_sci_style(sci, SCE_D_WORD3, GEANY_FILETYPES_D, 8);
	set_sci_style(sci, SCE_D_TYPEDEF, GEANY_FILETYPES_D, 9);
	set_sci_style(sci, SCE_D_STRING, GEANY_FILETYPES_D, 10);
	set_sci_style(sci, SCE_D_STRINGEOL, GEANY_FILETYPES_D, 11);
	set_sci_style(sci, SCE_D_CHARACTER, GEANY_FILETYPES_D, 12);
	set_sci_style(sci, SCE_D_OPERATOR, GEANY_FILETYPES_D, 13);
	set_sci_style(sci, SCE_D_IDENTIFIER, GEANY_FILETYPES_D, 14);
	set_sci_style(sci, SCE_D_COMMENTLINEDOC, GEANY_FILETYPES_D, 15);
	set_sci_style(sci, SCE_D_COMMENTDOCKEYWORD, GEANY_FILETYPES_D, 16);
	set_sci_style(sci, SCE_D_COMMENTDOCKEYWORDERROR, GEANY_FILETYPES_D, 17);
}


static void styleset_ferite_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_FERITE, 20);
	styleset_c_like_init(config, config_home, GEANY_FILETYPES_FERITE);

	style_sets[GEANY_FILETYPES_FERITE].keywords = g_new(gchar*, 4);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_FERITE, 0, "false null self super true abstract alias and arguments attribute_missing break case class closure conformsToProtocol constructor continue default deliver destructor diliver directive do else extends eval final fix for function global handle if iferr implements include instanceof isa method_missing modifies monitor namespace new or private protected protocol public raise recipient rename return static switch uses using while");
	get_keyfile_keywords(config, config_home, "keywords", "types", GEANY_FILETYPES_FERITE, 1, "boolean string number array object void");
	get_keyfile_keywords(config, config_home, "keywords", "docComment", GEANY_FILETYPES_FERITE, 2, "brief class declaration description end example extends function group implements modifies module namespace param protocol return return static type variable warning");
	style_sets[GEANY_FILETYPES_FERITE].keywords[3] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_FERITE].wordchars);
}


static void styleset_ferite(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_FERITE;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_CPP, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_FERITE].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_FERITE].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_FERITE].keywords[2]);

	styleset_c_like(sci, GEANY_FILETYPES_FERITE);
}


static void styleset_vhdl_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_VHDL, 15);

	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0xd00000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "comment_line_bang", "0x3f5fbf", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "stringeol", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "keyword", "0x001a7f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_VHDL].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "stdoperator", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "attribute", "0x804020", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "stdfunction", "0x808020", "0xffffff", "true", &style_sets[GEANY_FILETYPES_VHDL].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "stdpackage", "0x208020", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "stdtype", "0x208080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_VHDL].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "userword", "0x804020", "0xffffff", "true", &style_sets[GEANY_FILETYPES_VHDL].styling[14]);

	style_sets[GEANY_FILETYPES_VHDL].keywords = g_new(gchar*, 8);
	get_keyfile_keywords(config, config_home, "keywords", "keywords", GEANY_FILETYPES_VHDL, 0,
			"access after alias all architecture array assert attribute begin block \
			 body buffer bus case component configuration constant disconnect downto else elsif \
			 end entity exit file for function generate generic group guarded if impure in inertial \
			 inout is label library linkage literal loop map new next null of on open others out \
			 package port postponed procedure process pure range record register reject report \
			 return select severity shared signal subtype then to transport type unaffected units \
			 until use variable wait when while with");
	get_keyfile_keywords(config, config_home, "keywords", "operators", GEANY_FILETYPES_VHDL, 1,
			"abs and mod nand nor not or rem rol ror sla sll sra srl xnor xor");
	get_keyfile_keywords(config, config_home, "keywords", "attributes", GEANY_FILETYPES_VHDL, 2,
			"left right low high ascending image value pos val succ pred leftof rightof base range \
			 reverse_range length delayed stable quiet transaction event active last_event last_active \
			 last_value driving driving_value simple_name path_name instance_name");
	get_keyfile_keywords(config, config_home, "keywords", "std_functions", GEANY_FILETYPES_VHDL, 3,
			"now readline read writeline write endfile resolved to_bit to_bitvector to_stdulogic \
			 to_stdlogicvector to_stdulogicvector to_x01 to_x01z to_UX01 rising_edge falling_edge \
			 is_x shift_left shift_right rotate_left rotate_right resize to_integer to_unsigned \
			 to_signed std_match to_01");
	get_keyfile_keywords(config, config_home, "keywords", "std_packages", GEANY_FILETYPES_VHDL, 4,
			"std ieee work standard textio std_logic_1164 std_logic_arith std_logic_misc \
			 std_logic_signed std_logic_textio std_logic_unsigned numeric_bit numeric_std \
			 math_complex math_real vital_primitives vital_timing");
	get_keyfile_keywords(config, config_home, "keywords", "std_types", GEANY_FILETYPES_VHDL, 5,
			"boolean bit character severity_level integer real time delay_length natural positive \
			 string bit_vector file_open_kind file_open_status line text side width std_ulogic \
			 std_ulogic_vector std_logic std_logic_vector X01 X01Z UX01 UX01Z unsigned signed");
	get_keyfile_keywords(config, config_home, "keywords", "userwords", GEANY_FILETYPES_VHDL, 6, "");
	style_sets[GEANY_FILETYPES_VHDL].keywords[7] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_VHDL].wordchars);
}


static void styleset_vhdl(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_VHDL;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_VHDL, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_VHDL].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_VHDL].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_VHDL].keywords[2]);
	SSM(sci, SCI_SETKEYWORDS, 3, (sptr_t) style_sets[GEANY_FILETYPES_VHDL].keywords[3]);
	SSM(sci, SCI_SETKEYWORDS, 4, (sptr_t) style_sets[GEANY_FILETYPES_VHDL].keywords[4]);
	SSM(sci, SCI_SETKEYWORDS, 5, (sptr_t) style_sets[GEANY_FILETYPES_VHDL].keywords[5]);
	SSM(sci, SCI_SETKEYWORDS, 6, (sptr_t) style_sets[GEANY_FILETYPES_VHDL].keywords[6]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_VHDL, 0);
	set_sci_style(sci, SCE_VHDL_DEFAULT, GEANY_FILETYPES_VHDL, 0);
	set_sci_style(sci, SCE_VHDL_COMMENT, GEANY_FILETYPES_VHDL, 1);
	set_sci_style(sci, SCE_VHDL_COMMENTLINEBANG, GEANY_FILETYPES_VHDL, 2);
	set_sci_style(sci, SCE_VHDL_NUMBER, GEANY_FILETYPES_VHDL, 3);
	set_sci_style(sci, SCE_VHDL_STRING, GEANY_FILETYPES_VHDL, 4);
	set_sci_style(sci, SCE_VHDL_OPERATOR, GEANY_FILETYPES_VHDL, 5);
	set_sci_style(sci, SCE_VHDL_IDENTIFIER, GEANY_FILETYPES_VHDL, 6);
	set_sci_style(sci, SCE_VHDL_STRINGEOL, GEANY_FILETYPES_VHDL, 7);
	set_sci_style(sci, SCE_VHDL_KEYWORD, GEANY_FILETYPES_VHDL, 8);
	set_sci_style(sci, SCE_VHDL_STDOPERATOR, GEANY_FILETYPES_VHDL, 9);
	set_sci_style(sci, SCE_VHDL_ATTRIBUTE, GEANY_FILETYPES_VHDL, 10);
	set_sci_style(sci, SCE_VHDL_STDFUNCTION, GEANY_FILETYPES_VHDL, 11);
	set_sci_style(sci, SCE_VHDL_STDPACKAGE, GEANY_FILETYPES_VHDL, 12);
	set_sci_style(sci, SCE_VHDL_STDTYPE, GEANY_FILETYPES_VHDL, 13);
	set_sci_style(sci, SCE_VHDL_USERWORD, GEANY_FILETYPES_VHDL, 14);
}


static void styleset_js_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_JS, 20);
	styleset_c_like_init(config, config_home, GEANY_FILETYPES_JS);

	style_sets[GEANY_FILETYPES_JS].keywords = g_new(gchar*, 2);
	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_JS, 0, "\
			abs abstract acos anchor asin atan atan2 big bold boolean break byte \
			case catch ceil char charAt charCodeAt class concat const continue cos \
			Date debugger default delete do double else enum escape eval exp export \
			extends false final finally fixed float floor fontcolor fontsize for \
			fromCharCode function goto if implements import in indexOf Infinity \
			instanceof int interface isFinite isNaN italics join lastIndexOf length \
			link log long Math max MAX_VALUE min MIN_VALUE NaN native NEGATIVE_INFINITY \
			new null Number package parseFloat parseInt pop POSITIVE_INFINITY pow private \
			protected public push random return reverse round shift short sin slice small \
			sort splice split sqrt static strike string String sub substr substring sup \
			super switch synchronized tan this throw throws toLowerCase toString \
			toUpperCase transient true try typeof undefined unescape unshift valueOf \
			var void volatile while with");
	style_sets[GEANY_FILETYPES_JS].keywords[1] = NULL;

	get_keyfile_wordchars(config, config_home, &style_sets[GEANY_FILETYPES_JS].wordchars);
}


static void styleset_js(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_JS;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_CPP, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_JS].keywords[0]);

	styleset_c_like(sci, GEANY_FILETYPES_JS);
}


static void styleset_lua_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_LUA, 20);

	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0xd00000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "commentline", "0xd00000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "commentdoc", "0x3f5fbf", "0xffffff", "true", &style_sets[GEANY_FILETYPES_LUA].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x00007f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_LUA].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "character", "0x008000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "literalstring", "0x008020", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "preprocessor", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "stringeol", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_LUA].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "function_basic", "0x991111", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "function_other", "0x690000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "coroutines", "0x66005c", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[15]);
	get_keyfile_hex(config, config_home, "styling", "word5", "0x7979ff", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[16]);
	get_keyfile_hex(config, config_home, "styling", "word6", "0xad00ff", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[17]);
	get_keyfile_hex(config, config_home, "styling", "word7", "0x03D000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[18]);
	get_keyfile_hex(config, config_home, "styling", "word8", "0xff7600", "0xffffff", "false", &style_sets[GEANY_FILETYPES_LUA].styling[19]);

	style_sets[GEANY_FILETYPES_LUA].keywords = g_new(gchar*, 9);
	get_keyfile_keywords(config, config_home, "keywords", "keywords", GEANY_FILETYPES_LUA, 0,
			"and break do else elseif end false for function if \
			 in local nil not or repeat return then true until while");
	get_keyfile_keywords(config, config_home, "keywords", "function_basic", GEANY_FILETYPES_LUA, 1,
			"_VERSION assert collectgarbage dofile error gcinfo loadfile loadstring \
			 print rawget rawset require tonumber tostring type unpack \
			 _ALERT _ERRORMESSAGE _INPUT _PROMPT _OUTPUT \
			 _STDERR _STDIN _STDOUT call dostring foreach foreachi getn globals newtype \
			 sort tinsert tremove _G getfenv getmetatable ipairs loadlib next pairs pcall \
			 rawequal setfenv setmetatable xpcall string table math coroutine io os debug \
			 load module select");
	get_keyfile_keywords(config, config_home, "keywords", "function_other", GEANY_FILETYPES_LUA, 2,
			"abs acos asin atan atan2 ceil cos deg exp \
			 floor format frexp gsub ldexp log log10 max min mod rad random randomseed \
			 sin sqrt strbyte strchar strfind strlen strlower strrep strsub strupper tan \
			 string.byte string.char string.dump string.find string.len \
			 string.lower string.rep string.sub string.upper string.format string.gfind string.gsub \
			 table.concat table.foreach table.foreachi table.getn table.sort table.insert table.remove table.setn \
			 math.abs math.acos math.asin math.atan math.atan2 math.ceil math.cos math.deg math.exp \
			 math.floor math.frexp math.ldexp math.log math.log10 math.max math.min math.mod \
			 math.pi math.pow math.rad math.random math.randomseed math.sin math.sqrt math.tan \
			 string.gmatch string.match string.reverse table.maxn \
			 math.cosh math.fmod math.modf math.sinh math.tanh math.huge");
	get_keyfile_keywords(config, config_home, "keywords", "coroutines", GEANY_FILETYPES_LUA, 3,
			"openfile closefile readfrom writeto appendto remove rename flush seek tmpfile tmpname \
			 read write clock date difftime execute exit getenv setlocale time coroutine.create \
			 coroutine.resume coroutine.status coroutine.wrap coroutine.yield io.close io.flush \
			 io.input io.lines io.open io.output io.read io.tmpfile io.type io.write io.stdin \
			 io.stdout io.stderr os.clock os.date os.difftime os.execute os.exit os.getenv \
			 os.remove os.rename os.setlocale os.time os.tmpname coroutine.running package.cpath \
			 package.loaded package.loadlib package.path package.preload package.seeall io.popen");
	get_keyfile_keywords(config, config_home, "keywords", "user1", GEANY_FILETYPES_LUA, 4, "");
	get_keyfile_keywords(config, config_home, "keywords", "user2", GEANY_FILETYPES_LUA, 5, "");
	get_keyfile_keywords(config, config_home, "keywords", "user3", GEANY_FILETYPES_LUA, 6, "");
	get_keyfile_keywords(config, config_home, "keywords", "user4", GEANY_FILETYPES_LUA, 7, "");
	style_sets[GEANY_FILETYPES_LUA].keywords[8] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_LUA].wordchars);
}


static void styleset_lua(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_LUA;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_LUA, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_LUA].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_LUA].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_LUA].keywords[2]);
	SSM(sci, SCI_SETKEYWORDS, 3, (sptr_t) style_sets[GEANY_FILETYPES_LUA].keywords[3]);
	SSM(sci, SCI_SETKEYWORDS, 4, (sptr_t) style_sets[GEANY_FILETYPES_LUA].keywords[4]);
	SSM(sci, SCI_SETKEYWORDS, 5, (sptr_t) style_sets[GEANY_FILETYPES_LUA].keywords[5]);
	SSM(sci, SCI_SETKEYWORDS, 6, (sptr_t) style_sets[GEANY_FILETYPES_LUA].keywords[6]);
	SSM(sci, SCI_SETKEYWORDS, 7, (sptr_t) style_sets[GEANY_FILETYPES_LUA].keywords[7]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_LUA, 0);
	set_sci_style(sci, SCE_LUA_DEFAULT, GEANY_FILETYPES_LUA, 0);
	set_sci_style(sci, SCE_LUA_COMMENT, GEANY_FILETYPES_LUA, 1);
	set_sci_style(sci, SCE_LUA_COMMENTLINE, GEANY_FILETYPES_LUA, 2);
	set_sci_style(sci, SCE_LUA_COMMENTDOC, GEANY_FILETYPES_LUA, 3);
	set_sci_style(sci, SCE_LUA_NUMBER, GEANY_FILETYPES_LUA, 4);
	set_sci_style(sci, SCE_LUA_WORD, GEANY_FILETYPES_LUA, 5);
	set_sci_style(sci, SCE_LUA_STRING, GEANY_FILETYPES_LUA, 6);
	set_sci_style(sci, SCE_LUA_CHARACTER, GEANY_FILETYPES_LUA, 7);
	set_sci_style(sci, SCE_LUA_LITERALSTRING, GEANY_FILETYPES_LUA, 8);
	set_sci_style(sci, SCE_LUA_PREPROCESSOR, GEANY_FILETYPES_LUA, 9);
	set_sci_style(sci, SCE_LUA_OPERATOR, GEANY_FILETYPES_LUA, 10);
	set_sci_style(sci, SCE_LUA_IDENTIFIER, GEANY_FILETYPES_LUA, 11);
	set_sci_style(sci, SCE_LUA_STRINGEOL, GEANY_FILETYPES_LUA, 12);
	set_sci_style(sci, SCE_LUA_WORD2, GEANY_FILETYPES_LUA, 13);
	set_sci_style(sci, SCE_LUA_WORD3, GEANY_FILETYPES_LUA, 14);
	set_sci_style(sci, SCE_LUA_WORD4, GEANY_FILETYPES_LUA, 15);
	set_sci_style(sci, SCE_LUA_WORD5, GEANY_FILETYPES_LUA, 16);
	set_sci_style(sci, SCE_LUA_WORD6, GEANY_FILETYPES_LUA, 17);
	set_sci_style(sci, SCE_LUA_WORD7, GEANY_FILETYPES_LUA, 18);
	set_sci_style(sci, SCE_LUA_WORD8, GEANY_FILETYPES_LUA, 19);
}


static void styleset_basic_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_BASIC, 19);

	get_keyfile_hex(config, config_home, "styling", "default", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[0]);
	get_keyfile_hex(config, config_home, "styling", "comment", "0x808080", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[1]);
	get_keyfile_hex(config, config_home, "styling", "number", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[2]);
	get_keyfile_hex(config, config_home, "styling", "word", "0x00007f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_BASIC].styling[3]);
	get_keyfile_hex(config, config_home, "styling", "string", "0xff901e", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[4]);
	get_keyfile_hex(config, config_home, "styling", "preprocessor", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[5]);
	get_keyfile_hex(config, config_home, "styling", "operator", "0x301010", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[6]);
	get_keyfile_hex(config, config_home, "styling", "identifier", "0x000000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[7]);
	get_keyfile_hex(config, config_home, "styling", "date", "0x1a6500", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[8]);
	get_keyfile_hex(config, config_home, "styling", "stringeol", "0x000000", "0xe0c0e0", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[9]);
	get_keyfile_hex(config, config_home, "styling", "word2", "0x007f7f", "0xffffff", "true", &style_sets[GEANY_FILETYPES_BASIC].styling[10]);
	get_keyfile_hex(config, config_home, "styling", "word3", "0x991111", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[11]);
	get_keyfile_hex(config, config_home, "styling", "word4", "0x0000d0", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[12]);
	get_keyfile_hex(config, config_home, "styling", "constant", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[13]);
	get_keyfile_hex(config, config_home, "styling", "asm", "0x105090", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[14]);
	get_keyfile_hex(config, config_home, "styling", "label", "0x007f7f", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[15]);
	get_keyfile_hex(config, config_home, "styling", "error", "0xd00000", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[16]);
	get_keyfile_hex(config, config_home, "styling", "hexnumber", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[17]);
	get_keyfile_hex(config, config_home, "styling", "binnumber", "0x007f00", "0xffffff", "false", &style_sets[GEANY_FILETYPES_BASIC].styling[18]);

	style_sets[GEANY_FILETYPES_BASIC].keywords = g_new(gchar*, 5);
	get_keyfile_keywords(config, config_home, "keywords", "keywords", GEANY_FILETYPES_BASIC, 0,
			"as asm bit bitreset bitset byte case cint close cls color const \
			 continue cshort csign csng cubyte cuint culngint custom data \
			 dim do double  else elseif end enum environ eof err error exec exit exp \
			 export extern field fix for function get gosub goto hex hibyte hiword if iif imp \
			 input instr int integer is kill left len let lobyte loc local locate lof log long \
			 longint loop loword lset mklongint mks mkshort mod next not on once open or out \
			 pointer pos preserve preset private public put read redim rem reset restore return \
			 sizeof sleep space static step stop str string sub then time timer to type ubound \
			 ubyte ucase uinteger ulongint union unsigned until ushort using val val64 valint \
			 wait while with xor");
	get_keyfile_keywords(config, config_home, "keywords", "preprocessor", GEANY_FILETYPES_BASIC, 1,
			"#define defined #dynamic #else #endif #endmacro #error #if #ifdef #ifndef #inclib #include \
			 #libpath #line #macro #print #undef");
	get_keyfile_keywords(config, config_home, "keywords", "user1", GEANY_FILETYPES_BASIC, 2, "");
	get_keyfile_keywords(config, config_home, "keywords", "user2", GEANY_FILETYPES_BASIC, 3, "");
	style_sets[GEANY_FILETYPES_BASIC].keywords[4] = NULL;

	get_keyfile_wordchars(config, config_home,
		&style_sets[GEANY_FILETYPES_BASIC].wordchars);
}


static void styleset_basic(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_BASIC;

	styleset_common(sci, 5, ft_id);

	apply_filetype_properties(sci, SCLEX_FREEBASIC, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_BASIC].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_BASIC].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_BASIC].keywords[2]);
	SSM(sci, SCI_SETKEYWORDS, 3, (sptr_t) style_sets[GEANY_FILETYPES_BASIC].keywords[3]);

	set_sci_style(sci, STYLE_DEFAULT, GEANY_FILETYPES_BASIC, 0);
	set_sci_style(sci, SCE_B_DEFAULT, GEANY_FILETYPES_BASIC, 0);
	set_sci_style(sci, SCE_B_COMMENT, GEANY_FILETYPES_BASIC, 1);
	set_sci_style(sci, SCE_B_NUMBER, GEANY_FILETYPES_BASIC, 2);
	set_sci_style(sci, SCE_B_KEYWORD, GEANY_FILETYPES_BASIC, 3);
	set_sci_style(sci, SCE_B_STRING, GEANY_FILETYPES_BASIC, 4);
	set_sci_style(sci, SCE_B_PREPROCESSOR, GEANY_FILETYPES_BASIC, 5);
	set_sci_style(sci, SCE_B_OPERATOR, GEANY_FILETYPES_BASIC, 6);
	set_sci_style(sci, SCE_B_IDENTIFIER, GEANY_FILETYPES_BASIC, 7);
	set_sci_style(sci, SCE_B_DATE, GEANY_FILETYPES_BASIC, 8);
	set_sci_style(sci, SCE_B_STRINGEOL, GEANY_FILETYPES_BASIC, 9);
	set_sci_style(sci, SCE_B_KEYWORD2, GEANY_FILETYPES_BASIC, 10);
	set_sci_style(sci, SCE_B_KEYWORD3, GEANY_FILETYPES_BASIC, 11);
	set_sci_style(sci, SCE_B_KEYWORD4, GEANY_FILETYPES_BASIC, 12);
	set_sci_style(sci, SCE_B_CONSTANT, GEANY_FILETYPES_BASIC, 13);
	set_sci_style(sci, SCE_B_ASM, GEANY_FILETYPES_BASIC, 14); /* (still?) unused by the lexer */
	set_sci_style(sci, SCE_B_LABEL, GEANY_FILETYPES_BASIC, 15);
	set_sci_style(sci, SCE_B_ERROR, GEANY_FILETYPES_BASIC, 16);
	set_sci_style(sci, SCE_B_HEXNUMBER, GEANY_FILETYPES_BASIC, 17);
	set_sci_style(sci, SCE_B_BINNUMBER, GEANY_FILETYPES_BASIC, 18);
}

static void styleset_haxe_init(gint ft_id, GKeyFile *config, GKeyFile *config_home)
{
	new_style_array(GEANY_FILETYPES_HAXE, 20);
	styleset_c_like_init(config, config_home, GEANY_FILETYPES_HAXE);

	style_sets[GEANY_FILETYPES_HAXE].keywords = g_new(gchar*, 4);

	get_keyfile_keywords(config, config_home, "keywords", "primary", GEANY_FILETYPES_HAXE, 0, "\
			abstract break case catch class \
			continue default do else enum external extends \
			finally float for function goto if implements import in \
			interface new package protected public \
			return static super switch this throw throws \
			try type var while");

	get_keyfile_keywords(config, config_home, "keywords", "secondary", GEANY_FILETYPES_HAXE, 1, "\
			Bool Enum Float Int Null Void Dynamic String");

	get_keyfile_keywords(config, config_home, "keywords", "classes", GEANY_FILETYPES_HAXE, 2, "\
			Array ArrayAccess Class Date DateTools \
			EReg Enum Hash IntHash IntIter \
			Iterable Iterator Lambda List Math Protected \
			Reflect Std  StringBuf StringTools Type \
			UInt ValueType Void Xml XmlType");

	style_sets[GEANY_FILETYPES_HAXE].keywords[3] = NULL;

	get_keyfile_wordchars(config, config_home, &style_sets[GEANY_FILETYPES_HAXE].wordchars);
}


static void styleset_haxe(ScintillaObject *sci)
{
	const filetype_id ft_id = GEANY_FILETYPES_HAXE;

	styleset_common(sci, 5,ft_id);

	apply_filetype_properties(sci, SCLEX_CPP, ft_id);

	SSM(sci, SCI_SETKEYWORDS, 0, (sptr_t) style_sets[GEANY_FILETYPES_HAXE].keywords[0]);
	SSM(sci, SCI_SETKEYWORDS, 1, (sptr_t) style_sets[GEANY_FILETYPES_HAXE].keywords[1]);
	SSM(sci, SCI_SETKEYWORDS, 2, (sptr_t) style_sets[GEANY_FILETYPES_HAXE].keywords[2]);

	styleset_c_like(sci, GEANY_FILETYPES_HAXE);
}


/* lang_name is the name used for the styleset_foo_init function, e.g. foo. */
#define init_styleset_case(ft_id, lang_name) \
	case (ft_id): \
		styleset_ ## lang_name ## _init(filetype_idx, config, configh); \
		break;

/* Called by filetypes_load_config(). */
void highlighting_init_styles(gint filetype_idx, GKeyFile *config, GKeyFile *configh)
{
	/* All stylesets depend on filetypes.common */
	if (filetype_idx != GEANY_FILETYPES_NONE)
		filetypes_load_config(GEANY_FILETYPES_NONE);

	switch (filetype_idx)
	{
		init_styleset_case(GEANY_FILETYPES_NONE,	common);
		init_styleset_case(GEANY_FILETYPES_ASM,		asm);
		init_styleset_case(GEANY_FILETYPES_BASIC,	basic);
		init_styleset_case(GEANY_FILETYPES_C,		c);
		init_styleset_case(GEANY_FILETYPES_CAML,	caml);
		init_styleset_case(GEANY_FILETYPES_CONF,	conf);
		init_styleset_case(GEANY_FILETYPES_CPP,		cpp);
		init_styleset_case(GEANY_FILETYPES_CS,		cs);
		init_styleset_case(GEANY_FILETYPES_CSS,		css);
		init_styleset_case(GEANY_FILETYPES_D,		d);
		init_styleset_case(GEANY_FILETYPES_DIFF,	diff);
		init_styleset_case(GEANY_FILETYPES_DOCBOOK,	docbook);
		init_styleset_case(GEANY_FILETYPES_FERITE,	ferite);
		init_styleset_case(GEANY_FILETYPES_FORTRAN,	fortran);
		init_styleset_case(GEANY_FILETYPES_HASKELL,	haskell);
		init_styleset_case(GEANY_FILETYPES_HAXE,	haxe);
		init_styleset_case(GEANY_FILETYPES_HTML,	html);
		init_styleset_case(GEANY_FILETYPES_JAVA,	java);
		init_styleset_case(GEANY_FILETYPES_JS,		js);
		init_styleset_case(GEANY_FILETYPES_LATEX,	latex);
		init_styleset_case(GEANY_FILETYPES_LUA,		lua);
		init_styleset_case(GEANY_FILETYPES_MAKE,	makefile);
		init_styleset_case(GEANY_FILETYPES_OMS,		oms);
		init_styleset_case(GEANY_FILETYPES_PASCAL,	pascal);
		init_styleset_case(GEANY_FILETYPES_PERL,	perl);
		init_styleset_case(GEANY_FILETYPES_PHP,		php);
		init_styleset_case(GEANY_FILETYPES_PYTHON,	python);
		init_styleset_case(GEANY_FILETYPES_RUBY,	ruby);
		init_styleset_case(GEANY_FILETYPES_SH,		sh);
		init_styleset_case(GEANY_FILETYPES_SQL,		sql);
		init_styleset_case(GEANY_FILETYPES_TCL,		tcl);
		init_styleset_case(GEANY_FILETYPES_VHDL,	vhdl);
		init_styleset_case(GEANY_FILETYPES_XML,		markup);
	}
}


/* lang_name is the name used for the styleset_foo function, e.g. foo. */
#define styleset_case(ft_id, lang_name) \
	case (ft_id): \
		styleset_ ## lang_name (sci); \
		break;

void highlighting_set_styles(ScintillaObject *sci, gint filetype_idx)
{
	filetypes_load_config(filetype_idx);	/* load filetypes.ext */

	/* load tags files (some lexers highlight global typenames) */
	if (filetype_idx < GEANY_FILETYPES_NONE)
		symbols_global_tags_loaded(filetype_idx);

	switch (filetype_idx)
	{
		styleset_case(GEANY_FILETYPES_ASM,		asm);
		styleset_case(GEANY_FILETYPES_BASIC,	basic);
		styleset_case(GEANY_FILETYPES_C,		c);
		styleset_case(GEANY_FILETYPES_CAML,		caml);
		styleset_case(GEANY_FILETYPES_CONF,		conf);
		styleset_case(GEANY_FILETYPES_CPP,		cpp);
		styleset_case(GEANY_FILETYPES_CS,		cs);
		styleset_case(GEANY_FILETYPES_CSS,		css);
		styleset_case(GEANY_FILETYPES_D,		d);
		styleset_case(GEANY_FILETYPES_DIFF,		diff);
		styleset_case(GEANY_FILETYPES_DOCBOOK,	docbook);
		styleset_case(GEANY_FILETYPES_FERITE,	ferite);
		styleset_case(GEANY_FILETYPES_FORTRAN,	fortran);
		styleset_case(GEANY_FILETYPES_HASKELL,	haskell);
		styleset_case(GEANY_FILETYPES_HAXE,		haxe);
		styleset_case(GEANY_FILETYPES_HTML,		html);
		styleset_case(GEANY_FILETYPES_JAVA,		java);
		styleset_case(GEANY_FILETYPES_JS,		js);
		styleset_case(GEANY_FILETYPES_LATEX,	latex);
		styleset_case(GEANY_FILETYPES_LUA,		lua);
		styleset_case(GEANY_FILETYPES_MAKE,		makefile);
		styleset_case(GEANY_FILETYPES_OMS,		oms);
		styleset_case(GEANY_FILETYPES_PASCAL,	pascal);
		styleset_case(GEANY_FILETYPES_PERL,		perl);
		styleset_case(GEANY_FILETYPES_PHP,		php);
		styleset_case(GEANY_FILETYPES_PYTHON,	python);
		styleset_case(GEANY_FILETYPES_RUBY,		ruby);
		styleset_case(GEANY_FILETYPES_SH,		sh);
		styleset_case(GEANY_FILETYPES_SQL,		sql);
		styleset_case(GEANY_FILETYPES_TCL,		tcl);
		styleset_case(GEANY_FILETYPES_VHDL,		vhdl);
		styleset_case(GEANY_FILETYPES_XML,		xml);
		default:
		styleset_case(GEANY_FILETYPES_NONE,		none);
	}
}


/* Retrieve a style style_id for the filetype ft_id. If the style was not already initialised
 * (e.g. by by opening a file of this type), it will be initialised. The returned pointer is
 * owned by Geany and must not be freed.
 * style_id is a Scintilla lexer style, see scintilla/SciLexer.h */
const HighlightingStyle *highlighting_get_style(gint ft_id, gint style_id)
{
	if (ft_id < 0 || ft_id > GEANY_MAX_BUILT_IN_FILETYPES)
		return NULL;

	if (style_sets[ft_id].styling == NULL)
		filetypes_load_config(ft_id);

	/** TODO style_id might not be the real array index (Scintilla styles are not always synced
	  * with array indices) */
	return (const HighlightingStyle*) &style_sets[ft_id].styling[style_id];
}
