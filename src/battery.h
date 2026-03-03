#ifndef APPLICATION_BATTERY_H_
#define APPLICATION_BATTERY_H_

struct battery_measurement {
	/** Battery voltage in millivolts. */
	uint16_t mv;

	/** Battery level in parts per ten thousand (pptt).
	 *  0 = 0%, 10000 = 100%, e.g., 5000 = 50%
	 */
	uint16_t level;
};

/** Measure battery voltage and remaining level.
 *
 * Performs a complete battery measurement, sampling the voltage and
 * calculating the remaining capacity level based on the discharge curve.
 *
 * @param measurement	Pointer to battery_measurement struct to fill with results.
 *
 * @return	0 on success, negative error code on failure.
 *		Possible error codes:
 *		  -ENODEV: ADC device not ready
 *		  -EINVAL: Invalid measurement
 *		  Other ADC driver error codes
 */
int battery_measure(struct battery_measurement *measurement);

#endif /* APPLICATION_BATTERY_H_ */
