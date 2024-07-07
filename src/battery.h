/*
 * Copyright (c) 2018-2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATION_BATTERY_H_
#define APPLICATION_BATTERY_H_

/** Enable or disable measurement of the battery voltage.
 *
 * @param enable true to enable, false to disable
 *
 * @return zero on success, or a negative error code.
 */
int battery_measure_enable(bool enable);

/** Measure the battery voltage.
 *
 * @return the battery voltage in millivolts, or a negative error
 * code.
 */
int battery_sample(void);

/** A point in a battery discharge curve sequence.
 *
 * A discharge curve is defined as a sequence of these points, where
 * the first point has #lvl_pptt set to 10000 and the last point has
 * #lvl_pptt set to zero.  Both #lvl_pptt and #lvl_mV should be
 * monotonic decreasing within the sequence.
 */
struct battery_level_point
{
	/** Remaining life at #lvl_mV. */
	uint16_t lvl_pptt;

	/** Battery voltage at #lvl_pptt remaining life. */
	uint16_t lvl_mV;
};

/** Simple discharging curve for a typical CR2032 coin cell battery. 
*
* See https://github.com/stanvn/zigbee-plant-sensor/pull/4 
*/
static const struct battery_level_point discharge_curve_cr2032[] = {
	{10000, 3100},
	{9900, 3000},
	{9800, 2900},
	{9600, 2800},
	{2100, 2700},
	{1100, 2600},
	{700, 2500},
	{400, 2400},
	{200, 2300},
	{100, 2200},
	{0, 2000},
};

/** Calculate the estimated battery level based on a measured voltage.
 *
 * @param batt_mV a measured battery voltage level.
 *
 * @param curve the discharge curve for the type of battery installed
 * on the system.
 *
 * @return the estimated remaining capacity in parts per ten
 * thousand.
 */
unsigned int battery_level_pptt(unsigned int batt_mV,
				const struct battery_level_point *curve);


#endif /* APPLICATION_BATTERY_H_ */
