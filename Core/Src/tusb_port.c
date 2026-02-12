/*
 * tinyUSB Port Layer for STM32H5
 */

#include <stdint.h>
#include "tusb.h"

// Forward declaration for HAL function
extern uint32_t HAL_GetTick(void);

//--------------------------------------------------------------------+
// Board Porting API
//--------------------------------------------------------------------+

// Initialize board peripherals
void tusb_hal_init(void)
{
  // Nothing needed here, USB is already initialized by HAL
}

// Get current time in milliseconds (required by tinyUSB)
uint32_t tusb_time_millis_api(void)
{
  return HAL_GetTick();
}
