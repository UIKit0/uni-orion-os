#include "vm/page.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "string.h"


static unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
supl_pte* page_lookup (process_t *process, const uint32_t pg_no);

void supl_pt_init(struct hash *supl_pt)
{
	hash_init(supl_pt, page_hash, page_less, NULL);
}

void supl_pt_free(struct hash *h)
{
	struct hash_iterator i;
	hash_first (&i, h);
	while (hash_next (&i))
	{
		supl_pte *spte = hash_entry (hash_cur (&i), supl_pte, he);
		free(spte);
	}

}

static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
	const supl_pte *p = hash_entry (p_, supl_pte, he);
	return hash_int (p->virt_page_no);
}

/* Returns true if page a precedes page b. */
static bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
void *aux UNUSED)
{
	const supl_pte *a = hash_entry (a_, supl_pte, he);
	const supl_pte *b = hash_entry (b_, supl_pte, he);

	return a->virt_page_no < b->virt_page_no;
}


/*
 * Returns the supplemental page table entry, containing the given virtual address,
 * or a null pointer if no such page exists.
*/
supl_pte *
page_lookup (process_t *p, const uint32_t pg_no)
{
	struct supl_pte spte;
	struct hash_elem *e;

	spte.virt_page_no = pg_no;
	e = hash_find(&(p->supl_pt), &spte.he);

	return e != NULL ? hash_entry (e, supl_pte, he) : NULL ;
}

void supl_pt_insert(struct hash *spt, supl_pte *spte)
{
	//TODO: might need synch
	hash_insert(spt, &(spte->he));
}

void supl_pt_remove(struct hash *spt, supl_pte *spte)
{
	hash_delete(spt, &(spte->he));
}

supl_pte*
supl_pt_get_spte(process_t *p, void *uaddr)
{
	uint32_t pg_nr = pg_no(uaddr);
	return page_lookup(p, pg_nr);
}
