#
/*
 *    Copyright (C) 2013 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Programming
 *
 *    This file is part of the DAB-library
 *    DAB-library is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    DAB-library is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with DAB-library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#
#include	"dab-constants.h"
#include	"msc-handler.h"
#include	"dab-virtual.h"
#include	"dab-audio.h"
#include	"dab-data.h"
#include	"dab-params.h"
//
//	Interface program for processing the MSC.
//	Merely a dispatcher for the selected service
//
//	The ofdm processor assumes the existence of an msc-handler, whether
//	a service is selected or not. 

#define	CUSize	(4 * 16)
//	Note CIF counts from 0 .. 3
//
		mscHandler::mscHandler	(uint8_t	Mode,
	                                 audioOut_t	soundOut,
	                                 dataOut_t	dataOut,
	                                 bytesOut_t	bytesOut,
	                                 programQuality_t mscQuality,
	                                 motdata_t	motdata_Handler,
	                                 void		*userData):
	                                    params (Mode) {
	this	-> soundOut	= soundOut;
	this	-> dataOut	= dataOut;
	this	-> bytesOut	= bytesOut;
	this	-> programQuality	= mscQuality;
	this	-> motdata_Handler	= motdata_Handler;
	this	-> userData		= userData;

	cifVector. resize (55296);
	cifCount		= 0;	// msc blocks in CIF
	blkCount		= 0;
	dabHandler		= new dabVirtual;
	newChannel		= false;
	work_to_be_done		= false;
	dabModus		= 0;
	BitsperBlock		= 2 * params. get_carriers ();
	if (Mode == 4)	// 2 CIFS per 76 blocks
	   numberofblocksperCIF	= 36;
	else
	if (Mode == 1)	// 4 CIFS per 76 blocks
	   numberofblocksperCIF	= 18;
	else
	if (Mode == 2)	// 1 CIF per 76 blocks
	   numberofblocksperCIF	= 72;
	else			// shouldnot/cannot happen
	   numberofblocksperCIF	= 18;

	audioService		= true;		// default
}

		mscHandler::~mscHandler	(void) {
	dabHandler	-> stopRunning ();
	delete	dabHandler;
}

//
//	Note, the set_xxx functions are called from within a
//	different thread than the process_mscBlock method,
//	so, a little bit of locking seems wise while
//	the actual changing of the settings is done in the
//	thread executing process_mscBlock
void	mscHandler::set_audioChannel (audiodata *d) {
	mutexer. lock ();
	audioService	= true;
	new_shortForm	= d	-> shortForm;
	new_startAddr	= d	-> startAddr;
	new_Length	= d	-> length;
	new_protLevel	= d	-> protLevel;
	new_bitRate	= d	-> bitRate;
	new_language	= d	-> language;
	new_type	= d	-> programType;
	new_ASCTy	= d	-> ASCTy;
	new_dabModus	= new_ASCTy == 077 ? DAB_PLUS : DAB;
	newChannel	= true;
	mutexer. unlock ();
}


void	mscHandler::set_dataChannel (packetdata	*d) {
	mutexer. lock ();
	audioService	= false;
	new_shortForm	= d	-> shortForm;
	new_startAddr	= d	-> startAddr;
	new_Length	= d	-> length;
	new_protLevel	= d	-> protLevel;
	new_DGflag	= d	-> DGflag;
	new_bitRate	= d	-> bitRate;
	new_FEC_scheme	= d	-> FEC_scheme;
	new_DSCTy	= d	-> DSCTy;
	new_packetAddress = d	-> packetAddress;
	new_appType	= d	-> appType;
	newChannel	= true;
	mutexer. unlock ();
}

//
//	add blocks. First is (should be) block 5, last is (should be) 76
//	Note that this method is called from within the ofdm-processor thread
//	while the set_xxx methods are called from within the 
//	gui thread
//
//	Any change in the selected service will only be active
//	during te next process_mscBlock call.
void	mscHandler::process_mscBlock	(std::vector<int16_t> fbits,
	                                 int16_t blkno) { 
int16_t	currentblk;
int16_t	*myBegin;

	if (!work_to_be_done && !newChannel)
	   return;

	currentblk	= (blkno - 4) % numberofblocksperCIF;
//
	if (newChannel) {
	   mutexer. lock ();
	   newChannel	= false;
	   dabHandler -> stopRunning ();
	   delete dabHandler;

	   if (audioService) {
	      dabHandler = new dabAudio (new_dabModus,
	                                 new_Length * CUSize,
	                                 new_bitRate,
	                                 new_shortForm,
	                                 new_protLevel,
	                                 soundOut,
	                                 dataOut,
	                                 programQuality,
	                                 motdata_Handler,
	                                 userData
	                           );
	   }
	   else {
	      dabHandler = new dabData (new_DSCTy,
	                                new_appType,
	                                new_packetAddress,
	                                new_Length * CUSize,
	                                new_bitRate,
	                                new_shortForm,
	                                new_protLevel,
	                                new_DGflag,
	                                new_FEC_scheme,
	                                bytesOut,
	                                userData);
	   }
//
//	these we need for actual processing
	   startAddr	= new_startAddr;
	   Length	= new_Length;
//	and this one to get started
	   work_to_be_done	= true;
	   mutexer. unlock ();
	}
//
//	and the normal operation is:
	memcpy (&cifVector [currentblk * BitsperBlock],
	                    fbits. data (), BitsperBlock * sizeof (int16_t));
	if (currentblk < numberofblocksperCIF - 1) 
	   return;
//
//	OK, now we have a full CIF
	blkCount	= 0;
	cifCount	= (cifCount + 1) & 03;
	myBegin		= &cifVector [startAddr * CUSize];
//	Here we move the vector to be processed to a
//	separate task or separate function, depending on
//	the settings in the ini file, we might take advantage of multi cores
	(void) dabHandler -> process (myBegin, Length * CUSize);
}
//

void	mscHandler::stopProcessing (void) {
	work_to_be_done	= false;
}

void	mscHandler::stopHandler	(void) {
	work_to_be_done	= false;
	dabHandler	-> stopRunning ();
}

