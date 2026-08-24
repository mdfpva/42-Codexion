/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   run.c                                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 15:13:22 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 15:13:32 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

int	start_coders(t_sim *sim)
{
	int	i;

	i = 0;
	while (i < sim->n_coders)
	{
		if (pthread_create(&sim->coders[i].thread, NULL,
				coder_routine, &sim->coders[i]) != 0)
			return (i);
		i++;
	}
	return (i);
}

void	join_coders(t_sim *sim, int count)
{
	int	i;

	i = 0;
	while (i < count)
	{
		pthread_join(sim->coders[i].thread, NULL);
		i++;
	}
}

void	prime_deadlines(t_sim *sim)
{
	int	i;

	sim->start_ms = now_ms();
	i = 0;
	while (i < sim->n_coders)
	{
		sim->coders[i].last_compile_start = sim->start_ms;
		i++;
	}
}

int	run_sim(t_sim *sim)
{
	pthread_t	monitor;
	int			started;

	prime_deadlines(sim);
	if (pthread_create(&monitor, NULL, monitor_routine, sim) != 0)
		return (0);
	started = start_coders(sim);
	if (started < sim->n_coders)
		stop_sim(sim);
	pthread_join(monitor, NULL);
	join_coders(sim, started);
	return (1);
}
