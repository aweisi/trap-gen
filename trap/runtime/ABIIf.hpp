/***************************************************************************\
 *
 *
 *            ___        ___           ___           ___
 *           /  /\      /  /\         /  /\         /  /\
 *          /  /:/     /  /::\       /  /::\       /  /::\
 *         /  /:/     /  /:/\:\     /  /:/\:\     /  /:/\:\
 *        /  /:/     /  /:/~/:/    /  /:/~/::\   /  /:/~/:/
 *       /  /::\    /__/:/ /:/___ /__/:/ /:/\:\ /__/:/ /:/
 *      /__/:/\:\   \  \:\/:::::/ \  \:\/:/__\/ \  \:\/:/
 *      \__\/  \:\   \  \::/~~~~   \  \::/       \  \::/
 *           \  \:\   \  \:\        \  \:\        \  \:\
 *            \  \ \   \  \:\        \  \:\        \  \:\
 *             \__\/    \__\/         \__\/         \__\/
 *
 *
 *
 *
 *   This file is part of TRAP.
 *
 *   TRAP is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *   or see <http://www.gnu.org/licenses/>.
 *
 *
 *
 *   (c) Luca Fossati, fossati@elet.polimi.it
 *
\***************************************************************************/

#ifndef ABIIF_HPP
#define ABIIF_HPP

#include <vector>

template<class regWidth> class ABIIf{
    public:
    virtual bool isLittleEndian() = 0;
    virtual regWidth readLR() = 0;
    virtual void setLR( const regWidth & newValue ) = 0;
    virtual regWidth readPC() = 0;
    virtual void setPC( const regWidth & newValue ) = 0;
    virtual regWidth readSP() = 0;
    virtual void setSP( const regWidth & newValue ) = 0;
    virtual regWidth readFP() = 0;
    virtual void setFP( const regWidth & newValue ) = 0;
    virtual regWidth readRetVal() = 0;
    virtual void setRetVal( const regWidth & newValue ) = 0;
    virtual std::vector< regWidth > readArgs() = 0;
    virtual void setArgs( const std::vector< regWidth > & args ) = 0;
    virtual regWidth readGDBReg( const unsigned int & gdbId ) = 0;
    virtual void setGDBReg( const regWidth & newValue, const unsigned int & gdbId ) = 0;
    virtual regWidth readMem( const regWidth & address, int length = sizeof(regWidth) ) = 0;
    virtual unsigned char readCharMem( const regWidth & address) = 0;
    virtual void writeMem( const regWidth & address, const regWidth & datum, int length = sizeof(regWidth) ) = 0;
    virtual void writeCharMem( const regWidth & address, const unsigned char & datum ) = 0;
    virtual regWidth getCodeLimit() = 0;
    virtual ~ABIIf(){}
};

#endif