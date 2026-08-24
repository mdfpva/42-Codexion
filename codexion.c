/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   codexion.c                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/21 16:01:05 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 15:23:17 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

int	main(int ac, char **av)
{
	t_sim	sim;

	memset(&sim, 0, sizeof(t_sim));
	if (!parse_args(&sim, ac, av))
		return (1);
	if (!init_sim(&sim))
	{
		cleanup_sim(&sim);
		return (1);
	}
	run_sim(&sim);
	cleanup_sim(&sim);
	return (0);
}
