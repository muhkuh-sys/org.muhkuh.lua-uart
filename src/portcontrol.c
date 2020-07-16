
#include "portcontrol.h"

#include "netx_io_areas.h"



void portcontrol_apply(const unsigned short *pusIndex, const unsigned short *pusConfiguration, unsigned int sizConfiguration)
{
	const unsigned short *pusIndexCnt;
	const unsigned short *pusIndexEnd;
	const unsigned short *pusConfigurationCnt;
	unsigned long ulConfiguration;
	unsigned long ulOffset;
	volatile unsigned long *pulPortControl;


	pulPortControl = (volatile unsigned long*)HOSTADDR(PORTCONTROL);

	pusIndexCnt = pusIndex;
	pusIndexEnd = pusIndex + sizConfiguration;
	pusConfigurationCnt = pusConfiguration;
	while( pusIndexCnt<pusIndexEnd )
	{
		/* Get the value. */
		ulOffset = (unsigned long)(*(pusIndexCnt++));
		ulConfiguration = (unsigned long)(*(pusConfigurationCnt++));

		if( ulConfiguration!=PORTCONTROL_SKIP && ulOffset!=PORTCONTROL_SKIP )
		{
			/* Write the configuration. */
			pulPortControl[ulOffset] = ulConfiguration;
		}
	}
}



void portcontrol_apply_mmio(const unsigned char *pucMmioIndex, const unsigned short *pusConfiguration, unsigned int sizConfiguration)
{
	const unsigned char *pucMmioIndexCnt;
	const unsigned char *pucMmioIndexEnd;
	const unsigned short *pusConfigurationCnt;
	unsigned long ulConfiguration;
	unsigned long ulOffset;
	volatile unsigned long *pulPortControl;


	pulPortControl = (volatile unsigned long*)HOSTADDR(PORTCONTROL);

	pucMmioIndexCnt = pucMmioIndex;
	pucMmioIndexEnd = pucMmioIndex + sizConfiguration;
	pusConfigurationCnt = pusConfiguration;
	while( pucMmioIndexCnt<pucMmioIndexEnd )
	{
		ulOffset = (unsigned long)(*(pucMmioIndexCnt++));
		ulConfiguration = (unsigned long)(*(pusConfigurationCnt++));

		if( ulOffset!=0xffU && ulConfiguration!=PORTCONTROL_SKIP )
		{
			/* MMIO0 is at 16,11.
			 * From MMIO1 on the pins start at 3,0 and continue sequentially. */
			if( ulOffset==0 )
			{
				ulOffset += PORTCONTROL_INDEX(16,11);
			}
			else
			{
				ulOffset += PORTCONTROL_INDEX( 3, 0);
			}

			/* Write the configuration. */
			pulPortControl[ulOffset] = ulConfiguration;
		}
	}
}
