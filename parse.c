/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   parse.c                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 14:58:31 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 14:58:37 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

int	usage(void)
{
	fprintf(stderr, "usage: ./codexion n_coders time_to_burnout ");
	fprintf(stderr, "time_to_compile time_to_debug time_to_refactor ");
	fprintf(stderr, "n_compiles dongle_cooldown [fifo|edf]\n");
	return (0);
}

int	parse_long(char *s, long *out)
{
	long	v;
	int		i;

	v = 0;
	i = 0;
	if (!s[0])
		return (0);
	while (s[i])
	{
		if (s[i] < '0' || s[i] > '9')
			return (0);
		v = v * 10 + (s[i] - '0');
		if (v > 2147483647)
			return (0);
		i++;
	}
	*out = v;
	return (1);
}

int	parse_policy(t_sim *sim, char *s)
{
	if (strcmp(s, "fifo") == 0)
	{
		sim->policy = FIFO;
		return (1);
	}
	if (strcmp(s, "edf") == 0)
	{
		sim->policy = EDF;
		return (1);
	}
	return (0);
}

int	parse_times(t_sim *sim, char **av)
{
	if (!parse_long(av[2], &sim->t_burnout)
		|| !parse_long(av[3], &sim->t_compile)
		|| !parse_long(av[4], &sim->t_debug)
		|| !parse_long(av[5], &sim->t_refactor)
		|| !parse_long(av[7], &sim->cooldown))
		return (0);
	return (sim->t_burnout > 0);
}

int	parse_args(t_sim *sim, int ac, char **av)
{
	long	n;
	long	m;

	if (ac != 9)
		return (usage());
	if (!parse_long(av[1], &n) || n < 1 || n > 500)
		return (usage());
	if (!parse_times(sim, av))
		return (usage());
	if (!parse_long(av[6], &m) || m < 1)
		return (usage());
	if (!parse_policy(sim, av[8]))
		return (usage());
	sim->n_coders = (int)n;
	sim->must_compile = (int)m;
	return (1);
}
