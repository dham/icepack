# Copyright (C) 2017-2018 by Daniel Shapero <shapero@uw.edu>
#
# This file is part of icepack.
#
# icepack is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# The full text of the license can be found in the file LICENSE in the
# icepack source directory or at <http://www.gnu.org/licenses/>.

r"""Functions for reading/writing the ArcInfo ASCII format

This module contains functions for reading and writing raster data in the
`Arc/Info ASCII grid <https://en.wikipedia.org/wiki/Esri_grid>`_ format.
The Center for Remote Sensing of Ice Sheets releases their `gridded data
products <https://data.cresis.ku.edu/data/grids/>`_ in this format. The
Arc/Info ASCII format has the virtue of being human readable, easy to
parse, and readable by every geographic information system.
"""
import os.path
import numpy as np
import numpy.ma as ma
from icepack.grid import GridData

def write(filename_or_file, q, missing):
    r"""Write a gridded data set to an ArcInfo ASCII format file

    Parameters
    ----------
    filename_or_file : str or file-like
        either the name of the file or the opened output file
    q : icepack.grid.GridData
        the gridded data set to be written
    """
    ny, nx = q.shape

    if isinstance(filename_or_file, str):
        output_file = open(filename_or_file, 'w')
    else:
        output_file = filename_or_file

    output_file.write("ncols           {0}\n".format(nx))
    output_file.write("nrows           {0}\n".format(ny))
    output_file.write("xllcorner       {0}\n".format(q._origin[0]))
    output_file.write("yllcorner       {0}\n".format(q._origin[1]))
    output_file.write("cellsize        {0}\n".format(q._delta))
    output_file.write("NODATA_value    {0}\n".format(missing))

    for i in range(ny - 1, -1, -1):
        for j in range(nx):
            value = q[i, j] if not q.data[i, j] is ma.masked else missing
            output_file.write("{0} ".format(value))
        output_file.write("\n")


def read(filename_or_file):
    r"""Read an ArcInfo ASCII file into a gridded data set

    Parameters
    ----------
    filename_or_file : str or file-like
        either the name of the file or the opened input file

    Returns
    -------
    q : icepack.grid.GridData
        the gridded data set's coordinates, values, and missing data mask
    """
    if isinstance(filename_or_file, str):
        input_file = open(filename_or_file, 'r')
    else:
        input_file = filename_or_file

    def rd():
        return input_file.readline().split()[1]

    nx = int(rd())
    ny = int(rd())
    xo = float(rd())
    yo = float(rd())
    dx = float(rd())
    missing = float(rd())

    data = np.zeros((ny, nx), dtype = np.float64)
    for i in range(ny-1, -1, -1):
        data[i, :] = [float(q) for q in input_file.readline().split()]

    return GridData((xo, yo), dx, data, missing_data_value=missing)
