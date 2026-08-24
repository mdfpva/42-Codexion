/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   init.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 15:07:42 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 15:08:08 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

void	assign_dongles(t_sim *sim, int i)
{
	t_dongle	*left;
	t_dongle	*right;

	left = &sim->dongles[i];
	right = &sim->dongles[(i + 1) % sim->n_coders];
	if (left->id < right->id)
	{
		sim->coders[i].first = left;
		sim->coders[i].second = right;
	}
	else
	{
		sim->coders[i].first = right;
		sim->coders[i].second = left;
	}
}

int	init_dongles(t_sim *sim)
{
	int	i;

	sim->dongles = malloc(sizeof(t_dongle) * sim->n_coders);
	if (!sim->dongles)
		return (0);
	memset(sim->dongles, 0, sizeof(t_dongle) * sim->n_coders);
	i = 0;
	while (i < sim->n_coders)
	{
		sim->dongles[i].id = i;
		sim->dongles[i].available = 1;
		pthread_mutex_init(&sim->dongles[i].lock, NULL);
		pthread_cond_init(&sim->dongles[i].cond, NULL);
		if (!heap_init(&sim->dongles[i].queue, sim->n_coders, sim->policy))
			return (0);
		i++;
	}
	return (1);
}

int	init_coders(t_sim *sim)
{
	int	i;

	sim->coders = malloc(sizeof(t_coder) * sim->n_coders);
	if (!sim->coders)
		return (0);
	memset(sim->coders, 0, sizeof(t_coder) * sim->n_coders);
	i = 0;
	while (i < sim->n_coders)
	{
		sim->coders[i].id = i + 1;
		sim->coders[i].sim = sim;
		pthread_mutex_init(&sim->coders[i].state_lock, NULL);
		assign_dongles(sim, i);
		i++;
	}
	return (1);
}

int	init_sim(t_sim *sim)
{
	pthread_mutex_init(&sim->stop_lock, NULL);
	pthread_mutex_init(&sim->seq_lock, NULL);
	pthread_mutex_init(&sim->log_lock, NULL);
	if (!init_dongles(sim))
		return (0);
	if (!init_coders(sim))
		return (0);
	return (1);
}
