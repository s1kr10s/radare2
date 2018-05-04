/* radare - LGPL - Copyright 2009-2018 - pancake, nibble, defragger, ret2libc */

#include <r_anal.h>
#include <r_cons.h>

#if 0
DICT
====

refs
  10 -> 20 C
  16 -> 10 J
  20 -> 10 C

xrefs
  20 -> [ 10 C ]
  10 -> [ 16 J, 20 C ]

10: call 20
16: jmp 10
20: call 10
#endif

#define u64_to_key(x) (sdb_fmt ("%"PFMT64x, (x)))

#define ht_find_u64(_ht,_key,_found) (ht_find ((_ht), u64_to_key (_key), (_found)))
#define ht_insert_u64(_ht,_key,_value) (ht_insert ((_ht), u64_to_key (_key), _value))
#define ht_update_u64(_ht,_key,_value) (ht_update ((_ht), u64_to_key (_key), _value))
#define ht_delete_u64(_ht,_key) (ht_delete ((_ht), u64_to_key (_key)))

R_API RAnalRef *r_anal_ref_new() {
	RAnalRef *ref = R_NEW0 (RAnalRef);
	if (ref) {
		ref->addr = -1;
		ref->at = -1;
		ref->type = R_ANAL_REF_TYPE_CODE;
	}
	return ref;
}

R_API RList *r_anal_ref_list_new() {
	return r_list_newf (r_anal_ref_free);
}

R_API void r_anal_ref_free(void *ref) {
	free (ref);
}
void xrefs_ht_free(HtKv *kv) {
	free (kv->key);
	ht_free (kv->value);
	free (kv);
}

void xrefs_ref_free(HtKv *kv) {
	free (kv->key);
	r_anal_ref_free (kv->value);
	free (kv);
}

static const char *analref_toString(RAnalRefType type) {
	switch (type) {
	case R_ANAL_REF_TYPE_NULL:
		/* do nothing */
		break;
	case R_ANAL_REF_TYPE_CODE:
		return "code jmp";
	case R_ANAL_REF_TYPE_CALL:
		return "code call";
	case R_ANAL_REF_TYPE_DATA:
		return "data mem";
	case R_ANAL_REF_TYPE_STRING:
		return "data string";
	}
	return "unk";
}

static bool appendRef(RList *list, const char *k, RAnalRef *ref) {
	RAnalRef *cloned = r_anal_ref_new ();
	cloned->addr = ref->addr;
	cloned->at = ref->at;
	cloned->type = ref->type;
	r_list_append (list, cloned);
	return true;
}

static bool mylistrefs_cb(RList *list, const char *k, SdbHash *ht) {
	ht_foreach (ht, (HtForeachCallback)appendRef, list);
	return true;
}

static void listxrefs(SdbHash *m, ut64 addr, RList *list) {
	if (addr == UT64_MAX) {
		ht_foreach (m, (HtForeachCallback)mylistrefs_cb, list);
	} else {
		bool found;
		SdbHash *d = ht_find_u64 (m, addr, &found);
		if (!found) {
			return;
		}

		ht_foreach (d, (HtForeachCallback)appendRef, list);
	}
}

static void setxref(SdbHash *m, ut64 from, ut64 to, int type) {
	bool found;
	SdbHash *ht = ht_find_u64 (m, from, &found);

	if (!found) {
		ht = ht_new (NULL, xrefs_ref_free, NULL);
		if (!ht) {
			return;
		}

		ht_insert_u64 (m, from, ht);
	}

	RAnalRef *ref = r_anal_ref_new ();
	ref->at = from;
	ref->addr = to;
	ref->type = type;

	ht_update_u64 (ht, to, ref);
}

// set a reference from FROM to TO and a cross-reference(xref) from TO to FROM.
R_API int r_anal_xrefs_set(RAnal *anal, const RAnalRefType type, ut64 from, ut64 to) {
	if (!anal) {
		return false;
	}
	if (!anal->iob.is_valid_offset (anal->iob.io, from, 0)) {
		return false;
	}
	if (!anal->iob.is_valid_offset (anal->iob.io, to, 0)) {
		return false;
	}
	// unknown refs should not be stored. seems wrong
#if 0
	if (type == R_ANAL_REF_TYPE_NULL) {
		return false;
	}
#endif
	setxref (anal->dict_xrefs, to, from, type);
	setxref (anal->dict_refs, from, to, type);
	return true;
}

R_API int r_anal_xrefs_deln(RAnal *anal, const RAnalRefType type, ut64 from, ut64 to) {
	if (!anal) {
		return false;
	}
	ht_delete_u64 (anal->dict_refs, from);
	ht_delete_u64 (anal->dict_xrefs, to);
	return true;
}

R_API int r_anal_xref_del(RAnal *anal, ut64 from, ut64 to) {
	bool res = false;
	res |= r_anal_xrefs_deln (anal, R_ANAL_REF_TYPE_NULL, from, to);
	res |= r_anal_xrefs_deln (anal, R_ANAL_REF_TYPE_CODE, from, to);
	res |= r_anal_xrefs_deln (anal, R_ANAL_REF_TYPE_CALL, from, to);
	res |= r_anal_xrefs_deln (anal, R_ANAL_REF_TYPE_DATA, from, to);
	res |= r_anal_xrefs_deln (anal, R_ANAL_REF_TYPE_STRING, from, to);
	return res;
}

R_API int r_anal_xrefs_from(RAnal *anal, RList *list, const char *kind, const RAnalRefType type, ut64 addr) {
	listxrefs (anal->dict_refs, addr, list);
	return true;
}

R_API RList *r_anal_xrefs_get(RAnal *anal, ut64 to) {
	RList *list = r_anal_ref_list_new ();
	if (!list) {
		return NULL;
	}
	listxrefs (anal->dict_xrefs, to, list);
	if (r_list_empty (list)) {
		r_list_free (list);
		list = NULL;
	}
	return list;
}

R_API RList *r_anal_refs_get(RAnal *anal, ut64 from) {
	RList *list = r_anal_ref_list_new ();
	if (!list) {
		return NULL;
	}
	listxrefs (anal->dict_refs, from, list);
	if (r_list_empty (list)) {
		r_list_free (list);
		list = NULL;
	}
	return list;
}

R_API RList *r_anal_xrefs_get_from(RAnal *anal, ut64 to) {
	RList *list = r_anal_ref_list_new ();
	if (!list) {
		return NULL;
	}
	listxrefs (anal->dict_refs, to, list);
	if (r_list_empty (list)) {
		r_list_free (list);
		list = NULL;
	}
	return list;
}

R_API void r_anal_xrefs_list(RAnal *anal, int rad) {
	bool is_first = true;
	RListIter *iter;
	RAnalRef *ref;
	RList *list = r_anal_ref_list_new();
	listxrefs (anal->dict_xrefs, UT64_MAX, list);
	if (rad == 'j') {
		anal->cb_printf ("{");
	}
	r_list_foreach (list, iter, ref) {
		int t = ref->type ? ref->type: ' ';
		switch (rad) {
		case '*':
			anal->cb_printf ("ax%c 0x%"PFMT64x" 0x%"PFMT64x"\n",
				t, ref->at, ref->addr);
			break;
		case '\0':
			{
				char *name = anal->coreb.getNameDelta (anal->coreb.core, ref->at);
				r_str_replace_char (name, ' ', 0);
				anal->cb_printf ("%40s", name? name: "");
				free (name);
				anal->cb_printf (" 0x%"PFMT64x" -> %9s -> 0x%"PFMT64x, ref->at, analref_toString (t), ref->addr);
				name = anal->coreb.getNameDelta (anal->coreb.core, ref->addr);
				r_str_replace_char (name, ' ', 0);
				if (name && *name) {
					anal->cb_printf (" %s\n", name);
				} else {
					anal->cb_printf ("\n");
				}
				free (name);
			}
			break;
		case 'q':
			anal->cb_printf ("0x%08"PFMT64x" -> 0x%08"PFMT64x"  %s\n", ref->at, ref->addr, analref_toString (t));
			break;
		case 'j':
			{
				if (is_first) {
					is_first = false;
				} else {
					anal->cb_printf (",");
				}
				anal->cb_printf ("\"%"PFMT64d"\":%"PFMT64d, ref->at, ref->addr);
			}
			break;
		default:
			break;
		}
	}
	if (rad == 'j') {
		anal->cb_printf ("}\n");
	}
	r_list_free (list);
}

R_API const char *r_anal_xrefs_type_tostring(RAnalRefType type) {
	switch (type) {
	case R_ANAL_REF_TYPE_CODE:
		return "JMP";
	case R_ANAL_REF_TYPE_CALL:
		return "CALL";
	case R_ANAL_REF_TYPE_DATA:
		return "DATA";
	case R_ANAL_REF_TYPE_STRING:
		return "STRING";
	case R_ANAL_REF_TYPE_NULL:
	default:
		return "UNKNOWN";
	}
}

R_API bool r_anal_xrefs_init(RAnal *anal) {
	SdbHash *tmp;

	ht_free (anal->dict_refs);
	anal->dict_refs = NULL;
	ht_free (anal->dict_xrefs);
	anal->dict_xrefs = NULL;

	tmp = ht_new (NULL, xrefs_ht_free, NULL);
	if (!tmp) {
		return false;
	}
	anal->dict_refs = tmp;

	tmp = ht_new (NULL, xrefs_ht_free, NULL);
	if (!tmp) {
		ht_free (anal->dict_refs);
		anal->dict_refs = NULL;
		return false;
	}
	anal->dict_xrefs = tmp;
	return true;
}

R_API int r_anal_xrefs_count(RAnal *anal) {
	return anal->dict_xrefs->count;
}

static int ref_cmp(const RAnalRef *a, const RAnalRef *b) {
	if (a->at < b->at) {
		return -1;
	} else if (a->at > b->at) {
		return 1;
	} else {
		if (a->addr < b->addr) {
			return -1;
		} else if (a->addr > b->addr) {
			return 1;
		}
	}
	return 0;
}

static RList *fcn_get_refs(RAnalFunction *fcn, SdbHash *ht) {
	RListIter *iter;
	RAnalBlock *bb;
	RList *list = r_anal_ref_list_new ();
	if (!list) {
		return NULL;
	}

	r_list_foreach (fcn->bbs, iter, bb) {
		int i;

		for (i = 0; i < bb->ninstr; ++i) {
			ut64 at = bb->addr + r_anal_bb_offset_inst (bb, i);
			listxrefs (ht, at, list);
		}
	}
	return list;
}

R_API RList *r_anal_fcn_get_refs(RAnal *anal, RAnalFunction *fcn) {
	return fcn_get_refs (fcn, anal->dict_refs);
}

R_API RList *r_anal_fcn_get_xrefs(RAnal *anal, RAnalFunction *fcn) {
	return fcn_get_refs (fcn, anal->dict_xrefs);
}

R_API RList *r_anal_fcn_get_refs_sorted(RAnal *anal, RAnalFunction *fcn) {
	RList *l = r_anal_fcn_get_refs (anal, fcn);
	r_list_sort (l, (RListComparator)ref_cmp);
	return l;
}

R_API RList *r_anal_fcn_get_xrefs_sorted(RAnal *anal, RAnalFunction *fcn) {
	RList *l = r_anal_fcn_get_xrefs (anal, fcn);
	r_list_sort (l, (RListComparator)ref_cmp);
	return l;
}
