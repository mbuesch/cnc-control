#ifndef TINY_LIST_H_
#define TINY_LIST_H_

#include "util.h"


struct tiny_list {
	struct tiny_list *prev;
	struct tiny_list *next;
};

static inline void tlist_init(struct tiny_list *list)
{
	list->prev = list;
	list->next = list;
}

static inline bool tlist_is_empty(struct tiny_list *list)
{
	return list->next == list;
}

static inline void tlist_add_tail(struct tiny_list *e, struct tiny_list *list)
{
	e->prev = list->prev;
	list->prev->next = e;
	list->prev = e;
	e->next = list;
}

static inline void tlist_del(struct tiny_list *e)
{
	e->next->prev = e->prev;
	e->prev->next = e->next;
	e->prev = e;
	e->next = e;
}

static inline void tlist_move_tail(struct tiny_list *e, struct tiny_list *list)
{
	tlist_del(e);
	tlist_add_tail(e, list);
}

#define tlist_entry(p, type, member)	container_of(p, type, member)

#define tlist_first_entry(list, type, member)	\
	tlist_entry((list)->next, type, member)

#define tlist_last_entry(list, type, member)	\
	tlist_entry((list)->prev, type, member)

#define tlist_for_each(p, list, member)					\
	for (p = tlist_entry((list)->next, typeof(*p), member);		\
	     &p->member != (list);					\
	     p = tlist_entry(p->member.next, typeof(*p), member))

#define tlist_for_each_delsafe(p, tmp, list, member)			\
	for (p = tlist_entry((list)->next, typeof(*p), member),		\
	     tmp = tlist_entry(p->member.next, typeof(*p), member);	\
	     &p->member != (list);					\
	     p = tmp,							\
	     tmp = tlist_entry(tmp->member.next, typeof(*p), member))

#endif /* TINY_LIST_H_ */
