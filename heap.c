/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   heap.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/21 23:49:41 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/21 23:56:28 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

int	heap_init(t_heap *h, int capacity, int policy)
{
	h->items = malloc(sizeof(t_request *) * capacity);
	if (!h->items)
		return (0);
	h->size = 0;
	h->capacity = capacity;
	h->policy = policy;
	return (1);
}

void	heap_destroy(t_heap *h)
{
	free(h->items);
	h->items = NULL;
	h->size = 0;
	h->capacity = 0;
}

int	heap_less(t_request *a, t_request *b, int policy)
{
	if (policy == FIFO)
		return (a->seq < b->seq);
	if (a->deadline != b->deadline)
		return (a->deadline < b->deadline);
	if (a->seq != b->seq)
		return (a->seq < b->seq);
	return (a->coder_id < b->coder_id);
}

void	heap_swap(t_heap *h, int i, int j)
{
	t_request	*tmp;

	tmp = h->items[i];
	h->items[i] = h->items[j];
	h->items[j] = tmp;
	h->items[i]->idx = i;
	h->items[j]->idx = j;
}

t_request	*heap_peek(t_heap *h)
{
	if (h->size == 0)
		return (NULL);
	return (h->items[0]);
}
