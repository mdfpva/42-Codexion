/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   heap_ops.c                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/21 23:50:11 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/21 23:50:27 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

void	heap_up(t_heap *h, int i)
{
	int	parent;

	while (i > 0)
	{
		parent = (i - 1) / 2;
		if (!heap_less(h->items[i], h->items[parent], h->policy))
			return ;
		heap_swap(h, i, parent);
		i = parent;
	}
}

void	heap_down(t_heap *h, int i)
{
	int	best;
	int	c;

	best = i;
	c = 2 * i + 1;
	while (c < h->size)
	{
		if (heap_less(h->items[c], h->items[best], h->policy))
			best = c;
		c++;
		if (c < h->size && heap_less(h->items[c], h->items[best], h->policy))
			best = c;
		if (best == i)
			return ;
		heap_swap(h, i, best);
		i = best;
		c = 2 * i + 1;
	}
}

int	heap_push(t_heap *h, t_request *r)
{
	if (h->size >= h->capacity)
		return (0);
	h->items[h->size] = r;
	r->idx = h->size;
	h->size++;
	heap_up(h, h->size - 1);
	return (1);
}

t_request	*heap_pop(t_heap *h)
{
	t_request	*top;

	if (h->size == 0)
		return (NULL);
	top = h->items[0];
	h->size--;
	if (h->size > 0)
	{
		h->items[0] = h->items[h->size];
		h->items[0]->idx = 0;
		heap_down(h, 0);
	}
	top->idx = -1;
	return (top);
}

void	heap_remove(t_heap *h, t_request *r)
{
	int	i;

	i = r->idx;
	if (i < 0 || i >= h->size || h->items[i] != r)
		return ;
	h->size--;
	if (i != h->size)
	{
		h->items[i] = h->items[h->size];
		h->items[i]->idx = i;
		heap_up(h, i);
		heap_down(h, i);
	}
	r->idx = -1;
}
