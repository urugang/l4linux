#pragma once

static inline
unsigned long l4x_map_page_attr_to_l4(pte_t pte)
{
	switch (pte_val(pte) & _PAGE_CACHE_MASK) {
	case _PAGE_CACHE_WC: /* _PAGE_PWT */
		return L4_FPAGE_BUFFERABLE;
	case _PAGE_CACHE_UC_MINUS: /* _PAGE_PCD */
	case _PAGE_CACHE_UC: /* _PAGE_PCD | _PAGE_PWT */
		return L4_FPAGE_UNCACHEABLE;
	case _PAGE_CACHE_WB: /* 0 */
	default:
		return 0; /* same attrs as source */
	};
}
