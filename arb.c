/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   arb.c                                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 18:14:58 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 18:15:09 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

int	is_best(t_heap *h, t_request *req, long now)
{
	int	i;

	i = 0;
	while (i < h->size)
	{
		if (h->items[i] != req
			&& heap_less(h->items[i], req, h->policy)
			&& pair_free(h->items[i]->owner, now))
			return (0);
		i++;
	}
	return (1);
}

int	can_take_pair(t_coder *c, t_request *r1, t_request *r2, long now)
{
	if (!pair_free(c, now))
		return (0);
	return (is_best(&c->first->queue, r1, now)
		&& is_best(&c->second->queue, r2, now));
}
