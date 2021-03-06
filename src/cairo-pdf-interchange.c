/* -*- Mode: c; c-basic-offset: 4; indent-tabs-mode: t; tab-width: 8; -*- */
/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2016 Adrian Johnson
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is Adrian Johnson.
 *
 * Contributor(s):
 *	Adrian Johnson <ajohnson@redneon.com>
 */


/* PDF Document Interchange features:
 *  - metadata
 *  - document outline
 *  - tagged pdf
 *  - hyperlinks
 *  - page labels
 */

#include "cairoint.h"

#include "cairo-pdf.h"
#include "cairo-pdf-surface-private.h"

#include "cairo-array-private.h"
#include "cairo-error-private.h"
#include "cairo-output-stream-private.h"

static void
write_rect_to_pdf_quad_points (cairo_output_stream_t   *stream,
			       const cairo_rectangle_t *rect,
			       double                   surface_height)
{
    _cairo_output_stream_printf (stream,
				 "%f %f %f %f %f %f %f %f",
				 rect->x,
				 surface_height - rect->y,
				 rect->x + rect->width,
				 surface_height - rect->y,
				 rect->x + rect->width,
				 surface_height - (rect->y + rect->height),
				 rect->x,
				 surface_height - (rect->y + rect->height));
}

static void
write_rect_int_to_pdf_bbox (cairo_output_stream_t       *stream,
			    const cairo_rectangle_int_t *rect,
			    double                       surface_height)
{
    _cairo_output_stream_printf (stream,
				 "%d %f %d %f",
				 rect->x,
				 surface_height - (rect->y + rect->height),
				 rect->x + rect->width,
				 surface_height - rect->y);
}

static cairo_int_status_t
add_tree_node (cairo_pdf_surface_t           *surface,
	       cairo_pdf_struct_tree_node_t  *parent,
	       const char                    *name,
	       cairo_pdf_struct_tree_node_t **new_node)
{
    cairo_pdf_struct_tree_node_t *node;

    node = malloc (sizeof(cairo_pdf_struct_tree_node_t));
    if (unlikely (node == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    node->name = strdup (name);
    node->res = _cairo_pdf_surface_new_object (surface);
    if (node->res.id == 0)
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    node->parent = parent;
    cairo_list_init (&node->children);
    _cairo_array_init (&node->mcid, sizeof(struct page_mcid));
    memset (&node->annot, 0, sizeof(node->annot));
    cairo_list_init (&node->children);

    cairo_list_add_tail (&node->link, &parent->children);

    *new_node = node;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_bool_t
is_leaf_node (cairo_pdf_struct_tree_node_t *node)
{
    return node->parent && cairo_list_is_empty (&node->children) ;
}

static void
free_node (cairo_pdf_struct_tree_node_t *node)
{
    cairo_pdf_struct_tree_node_t *child, *next;

    if (!node)
	return;

    cairo_list_foreach_entry_safe (child, next, cairo_pdf_struct_tree_node_t,
				   &node->children, link)
    {
	cairo_list_del (&child->link);
	free_node (child);
    }
    free (node->name);
    _cairo_array_fini (&node->mcid);
    free (node);
}

static cairo_status_t
add_mcid_to_node (cairo_pdf_surface_t          *surface,
		  cairo_pdf_struct_tree_node_t *node,
		  int                           page,
		  int                          *mcid)
{
    struct page_mcid mcid_elem;
    cairo_int_status_t status;
    cairo_pdf_interchange_t *ic = &surface->interchange;

    status = _cairo_array_append (&ic->mcid_to_tree, &node);
    if (unlikely (status))
	return status;

    mcid_elem.page = page;
    mcid_elem.mcid = _cairo_array_num_elements (&ic->mcid_to_tree) - 1;
    *mcid = mcid_elem.mcid;
    return _cairo_array_append (&node->mcid, &mcid_elem);
}

static cairo_int_status_t
cairo_pdf_interchange_write_node_object (cairo_pdf_surface_t            *surface,
					 cairo_pdf_struct_tree_node_t   *node)
{
    struct page_mcid *mcid_elem;
    int i, num_mcid, first_page;
    cairo_pdf_resource_t *page_res;
    cairo_pdf_struct_tree_node_t *child;

    _cairo_pdf_surface_update_object (surface, node->res);
    _cairo_output_stream_printf (surface->output,
				 "%d 0 obj\n"
				 "<< /Type /StructElem\n"
				 "   /S /%s\n"
				 "   /P %d 0 R\n",
				 node->res.id,
				 node->name,
				 node->parent->res.id);

    if (! cairo_list_is_empty (&node->children)) {
	if (cairo_list_is_singular (&node->children)) {
	    child = cairo_list_first_entry (&node->children, cairo_pdf_struct_tree_node_t, link);
	    _cairo_output_stream_printf (surface->output, "   /K %d 0 R\n", child->res.id);
	} else {
	    _cairo_output_stream_printf (surface->output, "   /K [ ");

	    cairo_list_foreach_entry (child, cairo_pdf_struct_tree_node_t,
				      &node->children, link)
	    {
		_cairo_output_stream_printf (surface->output, "%d 0 R ", child->res.id);
	    }
	    _cairo_output_stream_printf (surface->output, "]\n");
	}
    } else {
	num_mcid = _cairo_array_num_elements (&node->mcid);
	if (num_mcid > 0 ) {
	    mcid_elem = _cairo_array_index (&node->mcid, 0);
	    first_page = mcid_elem->page;
	    page_res = _cairo_array_index (&surface->pages, first_page - 1);
	    _cairo_output_stream_printf (surface->output, "   /Pg %d 0 R\n", page_res->id);

	    if (num_mcid == 1 && node->annot.res.id == 0) {
		_cairo_output_stream_printf (surface->output, "   /K %d\n", mcid_elem->mcid);
	    } else {
		_cairo_output_stream_printf (surface->output, "   /K [ ");
		if (node->annot.res.id != 0) {
		    _cairo_output_stream_printf (surface->output,
						 "%d 0 R ",
						 node->annot.res.id);
		}
		for (i = 0; i < num_mcid; i++) {
		    mcid_elem = _cairo_array_index (&node->mcid, i);
		    page_res = _cairo_array_index (&surface->pages, mcid_elem->page - 1);
		    if (mcid_elem->page == first_page) {
			_cairo_output_stream_printf (surface->output, "%d ", mcid_elem->mcid);
		    } else {
			_cairo_output_stream_printf (surface->output,
						     "\n       << /Type /MCR /Pg %d 0 R /MCID %d >> ",
						     page_res->id,
						     mcid_elem->mcid);
		    }
		}
		_cairo_output_stream_printf (surface->output, "]\n");
	    }
	}
    }
    _cairo_output_stream_printf (surface->output,
				 ">>\n"
				 "endobj\n");

    return _cairo_output_stream_get_status (surface->output);
}

static cairo_int_status_t
cairo_pdf_interchange_write_annot (cairo_pdf_surface_t            *surface,
				   cairo_pdf_struct_tree_node_t   *node)
{
    cairo_pdf_resource_t res;
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;
    cairo_pdf_interchange_t *ic = &surface->interchange;
    int sp;
    char *dest = NULL;
    int i, num_rects, num_mcid;
    struct page_mcid *mcid_elem;

    num_mcid = _cairo_array_num_elements (&node->mcid);
    if (num_mcid == 0 )
	return status;

    mcid_elem = _cairo_array_index (&node->mcid, 0);
    if (mcid_elem->page != ic->annot_page)
	return status;

    num_rects = _cairo_array_num_elements (&node->annot.link_attrs.rects);
    if (strcmp (node->name, CAIRO_TAG_LINK) == 0 &&
	node->annot.link_attrs.link_type != TAG_LINK_EMPTY &&
	(node->annot.extents.valid || num_rects > 0))
    {
	res = _cairo_pdf_surface_new_object (surface);

	status = _cairo_array_append (&ic->parent_tree, &res);
	if (unlikely (status))
	    return status;

	sp = _cairo_array_num_elements (&ic->parent_tree) - 1;

	status = _cairo_array_append (&surface->page_annots, &res);
	if (unlikely (status))
	    return status;

	_cairo_pdf_surface_update_object (surface, res);
	_cairo_output_stream_printf (surface->output,
				     "%d 0 obj\n"
				     "<< /Type /Annot\n"
				     "   /Subtype /Link\n"
				     "   /StructParent %d\n",
				     res.id,
				     sp);

	if (num_rects > 0) {
	    cairo_rectangle_int_t bbox_rect;

	    _cairo_output_stream_printf (surface->output,
					 "   /QuadPoints [ ");
	    for (i = 0; i < num_rects; i++) {
		cairo_rectangle_t rectf;
		cairo_rectangle_int_t recti;

		_cairo_array_copy_element (&node->annot.link_attrs.rects, i, &rectf);
		_cairo_rectangle_int_from_double (&recti, &rectf);
		if (i == 0)
		    bbox_rect = recti;
		else
		    _cairo_rectangle_union (&bbox_rect, &recti);

		write_rect_to_pdf_quad_points (surface->output, &rectf, node->annot.page_height);
		_cairo_output_stream_printf (surface->output, " ");
	    }
	    _cairo_output_stream_printf (surface->output,
					 "]\n"
					 "   /Rect [ ");
	    write_rect_int_to_pdf_bbox (surface->output, &bbox_rect, node->annot.page_height);
	    _cairo_output_stream_printf (surface->output, " ]\n");
	} else {
	    _cairo_output_stream_printf (surface->output,
					 "   /Rect [ ");
	    write_rect_int_to_pdf_bbox (surface->output, &node->annot.extents.extents, node->annot.page_height);
	    _cairo_output_stream_printf (surface->output, " ]\n");
	}

	if (node->annot.link_attrs.dest) {
	    status = _cairo_utf8_to_pdf_string (node->annot.link_attrs.dest, &dest);
	    if (unlikely (status))
		return status;
	}

	if (node->annot.link_attrs.link_type == TAG_LINK_DEST) {
	    if (node->annot.link_attrs.dest) {
		_cairo_output_stream_printf (surface->output,
					     "   /Dest %s\n",
					     dest);
	    } else {
		cairo_pdf_resource_t res;
		int page = node->annot.link_attrs.page;

		if (page < 1 || page > (int)_cairo_array_num_elements (&surface->pages))
		    return CAIRO_INT_STATUS_TAG_ERROR;

		_cairo_array_copy_element (&surface->pages, page - 1, &res);
		_cairo_output_stream_printf (surface->output,
					     "   /Dest [%d 0 R /XYZ %f %f 0]\n",
					     res.id,
					     node->annot.link_attrs.pos.x,
					     node->annot.page_height - node->annot.link_attrs.pos.y);
	    }
	} else if (node->annot.link_attrs.link_type == TAG_LINK_URI) {
	    _cairo_output_stream_printf (surface->output,
					 "   /A <<\n"
					 "      /Type /Action\n"
					 "      /S /URI\n"
					 "      /URI (%s)\n"
					 "   >>\n",
					 node->annot.link_attrs.uri);
	} else if (node->annot.link_attrs.link_type == TAG_LINK_FILE) {
	    _cairo_output_stream_printf (surface->output,
					 "   /A <<\n"
					 "      /Type /Action\n"
					 "      /S /GoToR\n"
					 "      /F (%s)\n",
					 node->annot.link_attrs.file);
	    if (node->annot.link_attrs.dest) {
		_cairo_output_stream_printf (surface->output,
					     "      /D %s\n",
					     dest);
	    } else {
		_cairo_output_stream_printf (surface->output,
					     "      /D [%d %f %f ]\n",
					     node->annot.link_attrs.page,
					     node->annot.link_attrs.pos.x,
					     node->annot.link_attrs.pos.y);
	    }
	    _cairo_output_stream_printf (surface->output,
					 "   >>\n");
	}

	_cairo_output_stream_printf (surface->output,
				     "   /BS << /W 0 >>"
				     ">>\n"
				     "endobj\n");

	status = _cairo_output_stream_get_status (surface->output);

	free (dest);
    }

    return status;
}

static cairo_int_status_t
cairo_pdf_interchange_walk_struct_tree (cairo_pdf_surface_t          *surface,
					cairo_pdf_struct_tree_node_t *node,
					cairo_int_status_t (*func) (cairo_pdf_surface_t *surface,
								    cairo_pdf_struct_tree_node_t *node))
{
    cairo_int_status_t status;
    cairo_pdf_struct_tree_node_t *child;

    if (node->parent) {
	status = func (surface, node);
	if (unlikely (status))
	    return status;
    }

    cairo_list_foreach_entry (child, cairo_pdf_struct_tree_node_t,
			      &node->children, link)
    {
	status = cairo_pdf_interchange_walk_struct_tree (surface, child, func);
	if (unlikely (status))
	    return status;
    }

    return status;
}

static cairo_int_status_t
cairo_pdf_interchange_write_struct_tree (cairo_pdf_surface_t *surface)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_pdf_struct_tree_node_t *child;

    if (cairo_list_is_empty (&ic->struct_root->children))
	return CAIRO_STATUS_SUCCESS;

    surface->struct_tree_root = _cairo_pdf_surface_new_object (surface);
    ic->struct_root->res = surface->struct_tree_root;

    cairo_pdf_interchange_walk_struct_tree (surface, ic->struct_root, cairo_pdf_interchange_write_node_object);

    child = cairo_list_first_entry (&ic->struct_root->children, cairo_pdf_struct_tree_node_t, link);
    _cairo_pdf_surface_update_object (surface, surface->struct_tree_root);
    _cairo_output_stream_printf (surface->output,
				 "%d 0 obj\n"
				 "<< /Type /StructTreeRoot\n"
				 "   /ParentTree %d 0 R\n"
				 "   /K [ %d 0 R ]\n"
				 ">>\n"
				 "endobj\n",
				 surface->struct_tree_root.id,
				 ic->parent_tree_res.id,
				 child->res.id);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
cairo_pdf_interchange_write_page_annots (cairo_pdf_surface_t *surface)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;

    _cairo_array_truncate (&surface->page_annots, 0);
    ic->annot_page = _cairo_array_num_elements (&surface->pages);

    cairo_pdf_interchange_walk_struct_tree (surface, ic->struct_root, cairo_pdf_interchange_write_annot);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
cairo_pdf_interchange_write_page_parent_elems (cairo_pdf_surface_t *surface)
{
    int num_elems, i;
    cairo_pdf_struct_tree_node_t *node;
    cairo_pdf_resource_t res;
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;

    surface->page_parent_tree = -1;
    num_elems = _cairo_array_num_elements (&ic->mcid_to_tree);
    if (num_elems > 0) {
	res = _cairo_pdf_surface_new_object (surface);
	_cairo_output_stream_printf (surface->output,
				     "%d 0 obj\n"
				     "[\n",
				     res.id);
	for (i = 0; i < num_elems; i++) {
	    _cairo_array_copy_element (&ic->mcid_to_tree, i, &node);
	    _cairo_output_stream_printf (surface->output, "  %d 0 R\n", node->res.id);
	}
	_cairo_output_stream_printf (surface->output,
				     "]\n"
				     "endobj\n");
	status = _cairo_array_append (&ic->parent_tree, &res);
	surface->page_parent_tree = _cairo_array_num_elements (&ic->parent_tree) - 1;
    }

    return status;
}

static cairo_int_status_t
cairo_pdf_interchange_write_parent_tree (cairo_pdf_surface_t *surface)
{
    int num_elems, i;
    cairo_pdf_resource_t *res;
    cairo_pdf_interchange_t *ic = &surface->interchange;

    num_elems = _cairo_array_num_elements (&ic->parent_tree);
    if (num_elems > 0) {
	ic->parent_tree_res = _cairo_pdf_surface_new_object (surface);
	_cairo_output_stream_printf (surface->output,
				     "%d 0 obj\n"
				     "<< /Nums [\n",
				     ic->parent_tree_res.id);
	for (i = 0; i < num_elems; i++) {
	    res = _cairo_array_index (&ic->parent_tree, i);
	    if (res->id) {
		_cairo_output_stream_printf (surface->output,
					     "   %d %d 0 R\n",
					     i,
					     res->id);
	    }
	}
	_cairo_output_stream_printf (surface->output,
				     "  ]\n"
				     ">>\n"
				     "endobj\n");
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
cairo_pdf_interchange_write_outline (cairo_pdf_surface_t *surface)
{
    int num_elems, i;
    cairo_pdf_outline_entry_t *outline;
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_int_status_t status;
    char *name = NULL;
    char *dest = NULL;

    num_elems = _cairo_array_num_elements (&ic->outline);
    if (num_elems < 2)
	return CAIRO_INT_STATUS_SUCCESS;

    _cairo_array_copy_element (&ic->outline, 0, &outline);
    outline->res = _cairo_pdf_surface_new_object (surface);
    surface->outlines_dict_res = outline->res;
    _cairo_output_stream_printf (surface->output,
				 "%d 0 obj\n"
				 "<< /Type /Outlines\n"
				 "   /First %d 0 R\n"
				 "   /Last %d 0 R\n"
				 "   /Count %d\n"
				 ">>\n"
				 "endobj\n",
				 outline->res.id,
				 outline->first_child->res.id,
				 outline->last_child->res.id,
				 outline->count);

    for (i = 1; i < num_elems; i++) {
	_cairo_array_copy_element (&ic->outline, i, &outline);
	_cairo_pdf_surface_update_object (surface, outline->res);
	status = _cairo_utf8_to_pdf_string (outline->name, &name);
	if (unlikely (status))
	    return status;

	status = _cairo_utf8_to_pdf_string (outline->dest, &dest);
	if (unlikely (status))
	    return status;

	_cairo_output_stream_printf (surface->output,
				     "%d 0 obj\n"
				     "<< /Title %s\n"
				     "   /Parent %d 0 R\n",
				     outline->res.id,
				     name,
				     outline->parent->res.id);

	if (outline->prev) {
	    _cairo_output_stream_printf (surface->output,
					 "   /Prev %d 0 R\n",
					 outline->prev->res.id);
	}

	if (outline->next) {
	    _cairo_output_stream_printf (surface->output,
					 "   /Next %d 0 R\n",
					 outline->next->res.id);
	}

	if (outline->first_child) {
	    _cairo_output_stream_printf (surface->output,
					 "   /First %d 0 R\n"
					 "   /Last %d 0 R\n"
					 "   /Count %d\n",
					 outline->first_child->res.id,
					 outline->last_child->res.id,
					 outline->count);
	}

	if (outline->flags) {
	    int flags = 0;
	    if (outline->flags & CAIRO_BOOKMARK_FLAG_ITALIC)
		flags |= 1;
	    if (outline->flags & CAIRO_BOOKMARK_FLAG_BOLD)
		flags |= 2;
	    _cairo_output_stream_printf (surface->output,
					 "   /F %d\n",
					 flags);
	}

	_cairo_output_stream_printf (surface->output,
				     "   /Dest %s\n"
				     ">>\n"
				     "endobj\n",
				     dest);
	free (dest);
    }

    return status;
}

/*
 * Split a page label into a text prefix and numeric suffix. Leading '0's are
 * included in the prefix. eg
 *  "3"     => NULL,    3
 *  "cover" => "cover", 0
 *  "A-2"   => "A-",    2
 *  "A-002" => "A-00",  2
 */
static char *
split_label (const char* label, int *num)
{
    int len, i;

    *num = 0;
    len = strlen (label);
    if (len == 0)
	return NULL;

    i = len;
    while (i > 0 && _cairo_isdigit (label[i-1]))
	   i--;

    while (i < len && label[i] == '0')
	i++;

    if (i < len)
	sscanf (label + i, "%d", num);

    if (i > 0) {
	char *s;
	s = _cairo_malloc (i + 1);
	if (!s)
	    return NULL;

	memcpy (s, label, i);
	s[i] = 0;
	return s;
    }

    return NULL;
}

/* strcmp that handles NULL arguments */
static cairo_bool_t
strcmp_null (const char *s1, const char *s2)
{
    if (s1 && s2)
	return strcmp (s1, s2) == 0;

    if (!s1 && !s2)
	return TRUE;

    return FALSE;
}

static cairo_int_status_t
cairo_pdf_interchange_write_page_labels (cairo_pdf_surface_t *surface)
{
    int num_elems, i;
    char *label;
    char *prefix;
    char *prev_prefix;
    int num, prev_num;
    cairo_int_status_t status;

    num_elems = _cairo_array_num_elements (&surface->page_labels);
    if (num_elems > 0) {
	surface->page_labels_res = _cairo_pdf_surface_new_object (surface);
	_cairo_output_stream_printf (surface->output,
				     "%d 0 obj\n"
				     "<< /Nums [\n",
				     surface->page_labels_res.id);
	prefix = NULL;
	prev_prefix = NULL;
	num = 0;
	prev_num = 0;
	for (i = 0; i < num_elems; i++) {
	    _cairo_array_copy_element (&surface->page_labels, i, &label);
	    if (label) {
		prefix = split_label (label, &num);
	    } else {
		prefix = NULL;
		num = i + 1;
	    }

	    if (!strcmp_null (prefix, prev_prefix) || num != prev_num + 1) {
		_cairo_output_stream_printf (surface->output,  "   %d << ", i);

		if (num)
		    _cairo_output_stream_printf (surface->output,  "/S /D /St %d ", num);

		if (prefix) {
		    char *s;
		    status = _cairo_utf8_to_pdf_string (prefix, &s);
		    if (unlikely (status))
			return status;

		    _cairo_output_stream_printf (surface->output,  "/P %s ", s);
		    free (s);
		}

		_cairo_output_stream_printf (surface->output,  ">>\n");
	    }
	    free (prev_prefix);
	    prev_prefix = prefix;
	    prefix = NULL;
	    prev_num = num;
	}
	free (prefix);
	free (prev_prefix);
	_cairo_output_stream_printf (surface->output,
				     "  ]\n"
				     ">>\n"
				     "endobj\n");
    }

    return CAIRO_STATUS_SUCCESS;
}

static void
_collect_dest (void *entry, void *closure)
{
    cairo_pdf_named_dest_t *dest = entry;
    cairo_pdf_surface_t *surface = closure;
    cairo_pdf_interchange_t *ic = &surface->interchange;

    ic->sorted_dests[ic->num_dests++] = dest;
}

static int
_dest_compare (const void *a, const void *b)
{
    const cairo_pdf_named_dest_t * const *dest_a = a;
    const cairo_pdf_named_dest_t * const *dest_b = b;

    return strcmp ((*dest_a)->attrs.name, (*dest_b)->attrs.name);
}

static cairo_int_status_t
_cairo_pdf_interchange_write_document_dests (cairo_pdf_surface_t *surface)
{
    int i;
    cairo_pdf_interchange_t *ic = &surface->interchange;

    if (ic->num_dests == 0) {
	ic->dests_res.id = 0;
        return CAIRO_STATUS_SUCCESS;
    }

    ic->sorted_dests = calloc (ic->num_dests, sizeof (cairo_pdf_named_dest_t *));
    if (unlikely (ic->sorted_dests == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    ic->num_dests = 0;
    _cairo_hash_table_foreach (ic->named_dests, _collect_dest, surface);
    qsort (ic->sorted_dests, ic->num_dests, sizeof (cairo_pdf_named_dest_t *), _dest_compare);

    ic->dests_res = _cairo_pdf_surface_new_object (surface);
    _cairo_output_stream_printf (surface->output,
				 "%d 0 obj\n"
				 "<< /Names [\n",
				 ic->dests_res.id);
    for (i = 0; i < ic->num_dests; i++) {
	cairo_pdf_named_dest_t *dest = ic->sorted_dests[i];
	cairo_pdf_resource_t page_res;
	double x = 0;
	double y = 0;

	if (dest->extents.valid) {
	    x = dest->extents.extents.x;
	    y = dest->extents.extents.y;
	}

	if (dest->attrs.x_valid)
	    x = dest->attrs.x;

	if (dest->attrs.y_valid)
	    y = dest->attrs.y;

	_cairo_array_copy_element (&surface->pages, dest->page - 1, &page_res);
	_cairo_output_stream_printf (surface->output,
				     "   (%s) [ %d 0 R /XYZ %f %f ]\n",
				     dest->attrs.name,
				     page_res.id,
				     x,
				     surface->height - y);
    }
    _cairo_output_stream_printf (surface->output,
				     "  ]\n"
				     ">>\n"
				     "endobj\n");

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
cairo_pdf_interchange_write_names_dict (cairo_pdf_surface_t *surface)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_int_status_t status;

    status = _cairo_pdf_interchange_write_document_dests (surface);
    if (unlikely (status))
	return status;

    surface->names_dict_res.id = 0;
    if (ic->dests_res.id != 0) {
	surface->names_dict_res = _cairo_pdf_surface_new_object (surface);
	_cairo_output_stream_printf (surface->output,
				     "%d 0 obj\n"
				     "<< /Dests %d 0 R >>\n"
				     "endobj\n",
				     surface->names_dict_res.id,
				     ic->dests_res.id);
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
cairo_pdf_interchange_write_docinfo (cairo_pdf_surface_t *surface)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;

    surface->docinfo_res = _cairo_pdf_surface_new_object (surface);
    if (surface->docinfo_res.id == 0)
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    _cairo_output_stream_printf (surface->output,
				 "%d 0 obj\n"
				 "<< /Producer (cairo %s (http://cairographics.org))\n",
				 surface->docinfo_res.id,
				 cairo_version_string ());

    if (ic->docinfo.title)
	_cairo_output_stream_printf (surface->output, "   /Title %s\n", ic->docinfo.title);

    if (ic->docinfo.author)
	_cairo_output_stream_printf (surface->output, "   /Author %s\n", ic->docinfo.author);

    if (ic->docinfo.subject)
	_cairo_output_stream_printf (surface->output, "   /Subject %s\n", ic->docinfo.subject);

    if (ic->docinfo.keywords)
	_cairo_output_stream_printf (surface->output, "   /Keywords %s\n", ic->docinfo.keywords);

    if (ic->docinfo.creator)
	_cairo_output_stream_printf (surface->output, "   /Creator %s\n", ic->docinfo.creator);

    if (ic->docinfo.create_date)
	_cairo_output_stream_printf (surface->output, "   /CreationDate %s\n", ic->docinfo.create_date);

    if (ic->docinfo.mod_date)
	_cairo_output_stream_printf (surface->output, "   /ModDate %s\n", ic->docinfo.mod_date);

    _cairo_output_stream_printf (surface->output,
				 ">>\n"
				 "endobj\n");

    return CAIRO_STATUS_SUCCESS;
}

static void
init_named_dest_key (cairo_pdf_named_dest_t *dest)
{
    dest->base.hash = _cairo_hash_bytes (_CAIRO_HASH_INIT_VALUE,
					 dest->attrs.name,
					 strlen (dest->attrs.name));
}

static cairo_bool_t
_named_dest_equal (const void *key_a, const void *key_b)
{
    const cairo_pdf_named_dest_t *a = key_a;
    const cairo_pdf_named_dest_t *b = key_b;

    return strcmp (a->attrs.name, b->attrs.name) == 0;
}

static void
_named_dest_pluck (void *entry, void *closure)
{
    cairo_pdf_named_dest_t *dest = entry;
    cairo_hash_table_t *table = closure;

    _cairo_hash_table_remove (table, &dest->base);
    free (dest->attrs.name);
    free (dest);
}

static cairo_int_status_t
_cairo_pdf_interchange_begin_structure_tag (cairo_pdf_surface_t    *surface,
					    cairo_tag_type_t        tag_type,
					    const char             *name,
					    const char             *attributes)
{
    int page_num, mcid;
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;
    cairo_pdf_interchange_t *ic = &surface->interchange;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {
	status = add_tree_node (surface, ic->current_node, name, &ic->current_node);
	if (unlikely (status))
	    return status;

	_cairo_tag_stack_set_top_data (&ic->analysis_tag_stack, ic->current_node);

	if (tag_type & TAG_TYPE_LINK) {
	    status = _cairo_tag_parse_link_attributes (attributes, &ic->current_node->annot.link_attrs);
	    if (unlikely (status))
		return status;

	    ic->current_node->annot.page_height = surface->height;
	    cairo_list_add_tail (&ic->current_node->annot.extents.link, &ic->extents_list);
	}

    } else if (surface->paginated_mode == CAIRO_PAGINATED_MODE_RENDER) {
	ic->current_node = _cairo_tag_stack_top_elem (&ic->render_tag_stack)->data;
	assert (ic->current_node != NULL);
	if (is_leaf_node (ic->current_node)) {
	    page_num = _cairo_array_num_elements (&surface->pages);
	    add_mcid_to_node (surface, ic->current_node, page_num, &mcid);
	    status = _cairo_pdf_operators_tag_begin (&surface->pdf_operators, name, mcid);
	}
    }

    return status;
}

static cairo_int_status_t
_cairo_pdf_interchange_begin_dest_tag (cairo_pdf_surface_t    *surface,
				       cairo_tag_type_t        tag_type,
				       const char             *name,
				       const char             *attributes)
{
    cairo_pdf_named_dest_t *dest;
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {
	dest = calloc (1, sizeof (cairo_pdf_named_dest_t));
	if (unlikely (dest == NULL))
	    return _cairo_error (CAIRO_STATUS_NO_MEMORY);

	status = _cairo_tag_parse_dest_attributes (attributes, &dest->attrs);
	if (unlikely (status))
	    return status;

	dest->page_height = surface->height;
	dest->page = _cairo_array_num_elements (&surface->pages);
	init_named_dest_key (dest);
	status = _cairo_hash_table_insert (ic->named_dests, &dest->base);
	if (unlikely (status))
	    return status;

	_cairo_tag_stack_set_top_data (&ic->analysis_tag_stack, dest);
	cairo_list_add_tail (&dest->extents.link, &ic->extents_list);
	ic->num_dests++;
    }

    return status;
}

cairo_int_status_t
_cairo_pdf_interchange_tag_begin (cairo_pdf_surface_t    *surface,
				  const char             *name,
				  const char             *attributes)
{
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;
    cairo_tag_type_t tag_type;
    cairo_pdf_interchange_t *ic = &surface->interchange;
    void *ptr;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {
	status = _cairo_tag_stack_push (&ic->analysis_tag_stack, name, attributes);

    } else if (surface->paginated_mode == CAIRO_PAGINATED_MODE_RENDER) {
	status = _cairo_tag_stack_push (&ic->render_tag_stack, name, attributes);
	_cairo_array_copy_element (&ic->push_data, ic->push_data_index++, &ptr);
	_cairo_tag_stack_set_top_data (&ic->render_tag_stack, ptr);
    }

    if (unlikely (status))
	return status;

    tag_type = _cairo_tag_get_type (name);
    if (tag_type & TAG_TYPE_STRUCTURE) {
	status = _cairo_pdf_interchange_begin_structure_tag (surface, tag_type, name, attributes);
	if (unlikely (status))
	    return status;
    }

    if (tag_type & TAG_TYPE_DEST) {
	status = _cairo_pdf_interchange_begin_dest_tag (surface, tag_type, name, attributes);
	if (unlikely (status))
	    return status;
    }

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {
	ptr = _cairo_tag_stack_top_elem (&ic->analysis_tag_stack)->data;
	status = _cairo_array_append (&ic->push_data, &ptr);
    }

    return status;
}

static cairo_int_status_t
_cairo_pdf_interchange_end_structure_tag (cairo_pdf_surface_t    *surface,
					  cairo_tag_type_t        tag_type,
					  cairo_tag_stack_elem_t *elem)
{
    const cairo_pdf_struct_tree_node_t *node;
    struct tag_extents *tag, *next;
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;

    assert (elem->data != NULL);
    node = elem->data;
    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {
	if (tag_type & TAG_TYPE_LINK) {
	    cairo_list_foreach_entry_safe (tag, next, struct tag_extents,
					   &ic->extents_list, link) {
		if (tag == &node->annot.extents) {
		    cairo_list_del (&tag->link);
		    break;
		}
	    }
	}
    } else if (surface->paginated_mode == CAIRO_PAGINATED_MODE_RENDER) {
	if (is_leaf_node (ic->current_node)) {
	    status = _cairo_pdf_operators_tag_end (&surface->pdf_operators);
	    if (unlikely (status))
		return status;
	}
    }

    ic->current_node = ic->current_node->parent;
    assert (ic->current_node != NULL);

    return status;
}

static cairo_int_status_t
_cairo_pdf_interchange_end_dest_tag (cairo_pdf_surface_t    *surface,
				     cairo_tag_type_t        tag_type,
				     cairo_tag_stack_elem_t *elem)
{
    struct tag_extents *tag, *next;
    cairo_pdf_named_dest_t *dest;
    cairo_pdf_interchange_t *ic = &surface->interchange;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {
	assert (elem->data != NULL);
	dest = (cairo_pdf_named_dest_t *) elem->data;
	cairo_list_foreach_entry_safe (tag, next, struct tag_extents,
					   &ic->extents_list, link) {
	    if (tag == &dest->extents) {
		cairo_list_del (&tag->link);
		break;
	    }
	}
    }

    return CAIRO_STATUS_SUCCESS;
}

cairo_int_status_t
_cairo_pdf_interchange_tag_end (cairo_pdf_surface_t *surface,
				const char          *name)
{
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_tag_type_t tag_type;
    cairo_tag_stack_elem_t *elem;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
	status = _cairo_tag_stack_pop (&ic->analysis_tag_stack, name, &elem);
    else if (surface->paginated_mode == CAIRO_PAGINATED_MODE_RENDER)
	status = _cairo_tag_stack_pop (&ic->render_tag_stack, name, &elem);

    if (unlikely (status))
	return status;

    tag_type = _cairo_tag_get_type (name);
    if (tag_type & TAG_TYPE_STRUCTURE) {
	status = _cairo_pdf_interchange_end_structure_tag (surface, tag_type, elem);
	if (unlikely (status))
	    goto cleanup;
    }

    if (tag_type & TAG_TYPE_DEST) {
	status = _cairo_pdf_interchange_end_dest_tag (surface, tag_type, elem);
	if (unlikely (status))
	    goto cleanup;
    }

  cleanup:
    _cairo_tag_stack_free_elem (elem);

    return status;
}

cairo_int_status_t
_cairo_pdf_interchange_add_operation_extents (cairo_pdf_surface_t         *surface,
					      const cairo_rectangle_int_t *extents)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    struct tag_extents *tag;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {
	cairo_list_foreach_entry (tag, struct tag_extents, &ic->extents_list, link) {
	    if (tag->valid) {
		_cairo_rectangle_union (&tag->extents, extents);
	    } else {
		tag->extents = *extents;
		tag->valid = TRUE;
	    }
	}
    }

    return CAIRO_STATUS_SUCCESS;
}

cairo_int_status_t
_cairo_pdf_interchange_begin_page_content (cairo_pdf_surface_t *surface)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;
    int page_num, mcid;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {
	_cairo_array_truncate (&ic->mcid_to_tree, 0);
	_cairo_array_truncate (&ic->push_data, 0);
	ic->begin_page_node = ic->current_node;
    } else if (surface->paginated_mode == CAIRO_PAGINATED_MODE_RENDER) {
	ic->push_data_index = 0;
	ic->current_node = ic->begin_page_node;
	if (ic->end_page_node && is_leaf_node (ic->end_page_node)) {
	    page_num = _cairo_array_num_elements (&surface->pages);
	    add_mcid_to_node (surface, ic->end_page_node, page_num, &mcid);
	    status = _cairo_pdf_operators_tag_begin (&surface->pdf_operators,
						     ic->end_page_node->name,
						     mcid);
	}
    }

    return status;
}

cairo_int_status_t
_cairo_pdf_interchange_end_page_content (cairo_pdf_surface_t *surface)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_RENDER) {
	ic->end_page_node = ic->current_node;
	if (is_leaf_node (ic->current_node))
	    status = _cairo_pdf_operators_tag_end (&surface->pdf_operators);
    }

    return status;
}

cairo_int_status_t
_cairo_pdf_interchange_write_page_objects (cairo_pdf_surface_t *surface)
{
    cairo_int_status_t status;

    status = cairo_pdf_interchange_write_page_annots (surface);
     if (unlikely (status))
	return status;

    return cairo_pdf_interchange_write_page_parent_elems (surface);
}

cairo_int_status_t
_cairo_pdf_interchange_write_document_objects (cairo_pdf_surface_t *surface)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_int_status_t status = CAIRO_STATUS_SUCCESS;

    status = cairo_pdf_interchange_write_parent_tree (surface);
    if (unlikely (status))
	return status;

    status = cairo_pdf_interchange_write_struct_tree (surface);
    if (unlikely (status))
	return status;

    if (_cairo_tag_stack_get_structure_type (&ic->analysis_tag_stack) == TAG_TREE_TYPE_TAGGED)
	surface->tagged = TRUE;

    status = cairo_pdf_interchange_write_outline (surface);
    if (unlikely (status))
	return status;

    status = cairo_pdf_interchange_write_page_labels (surface);
    if (unlikely (status))
	return status;

    status = cairo_pdf_interchange_write_names_dict (surface);
    if (unlikely (status))
	return status;

    status = cairo_pdf_interchange_write_docinfo (surface);

    return status;
}

cairo_int_status_t
_cairo_pdf_interchange_init (cairo_pdf_surface_t *surface)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_pdf_outline_entry_t *outline_root;
    cairo_int_status_t status;

    _cairo_tag_stack_init (&ic->analysis_tag_stack);
    _cairo_tag_stack_init (&ic->render_tag_stack);
    _cairo_array_init (&ic->push_data, sizeof(void *));
    ic->struct_root = calloc (1, sizeof(cairo_pdf_struct_tree_node_t));
    if (unlikely (ic->struct_root == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    cairo_list_init (&ic->struct_root->children);
    _cairo_array_init (&ic->struct_root->mcid, sizeof(struct page_mcid));
    ic->current_node = ic->struct_root;
    ic->begin_page_node = NULL;
    ic->end_page_node = NULL;
    _cairo_array_init (&ic->parent_tree, sizeof(cairo_pdf_resource_t));
    _cairo_array_init (&ic->mcid_to_tree, sizeof(cairo_pdf_struct_tree_node_t *));
    ic->parent_tree_res.id = 0;
    cairo_list_init (&ic->extents_list);
    ic->named_dests = _cairo_hash_table_create (_named_dest_equal);
    if (unlikely (ic->named_dests == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    ic->num_dests = 0;
    ic->sorted_dests = NULL;
    ic->dests_res.id = 0;

    _cairo_array_init (&ic->outline, sizeof(cairo_pdf_outline_entry_t *));
    outline_root = calloc (1, sizeof(cairo_pdf_outline_entry_t));
    if (unlikely (outline_root == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    memset (&ic->docinfo, 0, sizeof (ic->docinfo));
    status = _cairo_array_append (&ic->outline, &outline_root);

    return status;
}

cairo_int_status_t
_cairo_pdf_interchange_fini (cairo_pdf_surface_t *surface)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    unsigned i;

    _cairo_tag_stack_fini (&ic->analysis_tag_stack);
    _cairo_tag_stack_fini (&ic->render_tag_stack);
    _cairo_array_fini (&ic->push_data);
    free_node (ic->struct_root);
    _cairo_array_fini (&ic->mcid_to_tree);
    _cairo_array_fini (&ic->parent_tree);
    _cairo_hash_table_foreach (ic->named_dests, _named_dest_pluck, ic->named_dests);
    _cairo_hash_table_destroy (ic->named_dests);
    free (ic->sorted_dests);

    for (i = 0; i < _cairo_array_num_elements (&ic->outline); i++) {
	cairo_pdf_outline_entry_t *outline;

	_cairo_array_copy_element (&ic->outline, i, &outline);
	free (outline);
    }
    _cairo_array_fini (&ic->outline);
    free (ic->docinfo.title);
    free (ic->docinfo.author);
    free (ic->docinfo.subject);
    free (ic->docinfo.keywords);
    free (ic->docinfo.creator);
    free (ic->docinfo.create_date);
    free (ic->docinfo.mod_date);

    return CAIRO_STATUS_SUCCESS;
}

cairo_int_status_t
_cairo_pdf_interchange_add_outline (cairo_pdf_surface_t        *surface,
				    int                         parent_id,
				    const char                 *name,
				    const char                 *dest,
				    cairo_pdf_outline_flags_t   flags,
				    int                        *id)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_pdf_outline_entry_t *outline;
    cairo_pdf_outline_entry_t *parent;
    cairo_int_status_t status;

    if (parent_id < 0 || parent_id >= (int)_cairo_array_num_elements (&ic->outline))
	return CAIRO_STATUS_SUCCESS;

    outline = _cairo_malloc (sizeof(cairo_pdf_outline_entry_t));
    if (unlikely (outline == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    outline->res = _cairo_pdf_surface_new_object (surface);
    if (outline->res.id == 0)
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    outline->name = strdup (name);
    outline->dest = strdup (dest);
    outline->flags = flags;
    outline->count = 0;

    _cairo_array_copy_element (&ic->outline, parent_id, &parent);

    outline->parent = parent;
    outline->first_child = NULL;
    outline->last_child = NULL;
    outline->next = NULL;
    if (parent->last_child) {
	parent->last_child->next = outline;
	outline->prev = parent->last_child;
	parent->last_child = outline;
    } else {
	parent->first_child = outline;
	parent->last_child = outline;
	outline->prev = NULL;
    }

    *id = _cairo_array_num_elements (&ic->outline);
    status = _cairo_array_append (&ic->outline, &outline);
    if (unlikely (status))
	return status;

    /* Update Count */
    outline = outline->parent;
    while (outline) {
	if (outline->flags & CAIRO_BOOKMARK_FLAG_OPEN) {
	    outline->count++;
	} else {
	    outline->count--;
	    break;
	}
	outline = outline->parent;
    }

    return CAIRO_STATUS_SUCCESS;
}

/*
 * Date must be in the following format:
 *
 *     YYYY-MM-DDThh:mm:ss[Z+-]hh:mm
 *
 * Only the year is required. If a field is included all preceding
 * fields must be included.
 */
static char *
iso8601_to_pdf_date_string (const char *iso)
{
    char buf[40];
    const char *p;
    int i;

    /* Check that utf8 contains only the characters "0123456789-T:Z+" */
    p = iso;
    while (*p) {
       if (!_cairo_isdigit (*p) && *p != '-' && *p != 'T' &&
           *p != ':' && *p != 'Z' && *p != '+')
           return NULL;
       p++;
    }

    p = iso;
    strcpy (buf, "(");

   /* YYYY (required) */
    if (strlen (p) < 4)
       return NULL;

    strncat (buf, p, 4);
    p += 4;

    /* -MM, -DD, Thh, :mm, :ss */
    for (i = 0; i < 5; i++) {
	if (strlen (p) < 3)
	    goto finish;

	strncat (buf, p + 1, 2);
	p += 3;
    }

    /* Z, +, - */
    if (strlen (p) < 1)
       goto finish;
    strncat (buf, p, 1);
    p += 1;

    /* hh */
    if (strlen (p) < 2)
	goto finish;

    strncat (buf, p, 2);
    strcat (buf, "'");
    p += 2;

    /* :mm */
    if (strlen (p) < 3)
	goto finish;

    strncat (buf, p + 1, 3);

  finish:
    strcat (buf, ")");
    return strdup (buf);
}

cairo_int_status_t
_cairo_pdf_interchange_set_metadata (cairo_pdf_surface_t  *surface,
				     cairo_pdf_metadata_t  metadata,
				     const char           *utf8)
{
    cairo_pdf_interchange_t *ic = &surface->interchange;
    cairo_status_t status;
    char *s = NULL;

    if (utf8) {
	if (metadata == CAIRO_PDF_METADATA_CREATE_DATE ||
	    metadata == CAIRO_PDF_METADATA_MOD_DATE) {
	    s = iso8601_to_pdf_date_string (utf8);
	} else {
	    status = _cairo_utf8_to_pdf_string (utf8, &s);
	    if (unlikely (status))
		return status;
	}
    }

    switch (metadata) {
	case CAIRO_PDF_METADATA_TITLE:
	    free (ic->docinfo.title);
	    ic->docinfo.title = s;
	    break;
	case CAIRO_PDF_METADATA_AUTHOR:
	    free (ic->docinfo.author);
	    ic->docinfo.author = s;
	    break;
	case CAIRO_PDF_METADATA_SUBJECT:
	    free (ic->docinfo.subject);
	    ic->docinfo.subject = s;
	    break;
	case CAIRO_PDF_METADATA_KEYWORDS:
	    free (ic->docinfo.keywords);
	    ic->docinfo.keywords = s;
	    break;
	case CAIRO_PDF_METADATA_CREATOR:
	    free (ic->docinfo.creator);
	    ic->docinfo.creator = s;
	    break;
	case CAIRO_PDF_METADATA_CREATE_DATE:
	    free (ic->docinfo.create_date);
	    ic->docinfo.create_date = s;
	    break;
	case CAIRO_PDF_METADATA_MOD_DATE:
	    free (ic->docinfo.mod_date);
	    ic->docinfo.mod_date = s;
	    break;
    }

    return CAIRO_STATUS_SUCCESS;
}
