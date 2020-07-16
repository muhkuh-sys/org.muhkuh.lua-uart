/***************************************************************************
 *   Copyright (C) 2013 by Christoph Thelen                                *
 *   doc_bacardi@users.sourceforge.net                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include "header.h"
#include "version.h"


extern unsigned long load_address[];
unsigned long start(unsigned long ulParameter);
extern unsigned long parameter_start_address[];
extern unsigned long parameter_end_address[];


const VERSION_HEADER_T tVersionHeader =
{
	.acMagic = { 'm', 'o', 'o', 'h' },
	.ulVersion = 0x00010001,

	.pulLoadAddress = load_address,
	.pfnExecutionAddress = start,
	.pulParameterStart = parameter_start_address,
	.pulParameterEnd = parameter_end_address,

	.ulVersionMajor = VERSION_MAJOR,
	.ulVersionMinor = VERSION_MINOR,
	.ulVersionMicro = VERSION_MICRO,
	.acVersionVcs = VERSION_VCS
};

