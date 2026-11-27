package io.github.valeronm.sparedongle.ui

import java.util.Locale

private const val MS_PER_SECOND = 1_000L
private const val SECONDS_PER_MINUTE = 60L
private const val MINUTES_PER_HOUR = 60L
private const val SECONDS_PER_HOUR = 3_600L
private const val SECONDS_PER_DAY = 86_400L
private const val HOURS_PER_DAY = 24L
internal const val FULL_PERCENT = 100

/** Identifiers keep Latin digits whatever the locale. */
fun latinFormat(format: String, vararg args: Any): String = String.format(Locale.ROOT, format, *args)

/** [daysFormat] takes days, hours, minutes and seconds; [format] the last three. */
fun formatUptime(ms: Long, daysFormat: String, format: String): String {
    val s = ms / MS_PER_SECOND
    val days = s / SECONDS_PER_DAY
    val h = s / SECONDS_PER_HOUR % HOURS_PER_DAY
    val m = s / SECONDS_PER_MINUTE % MINUTES_PER_HOUR
    val sec = s % SECONDS_PER_MINUTE
    return if (days > 0) latinFormat(daysFormat, days, h, m, sec) else latinFormat(format, h, m, sec)
}

fun batteryBarIndex(percent: Int, bars: Int): Int =
    (percent.coerceIn(0, FULL_PERCENT) * (bars - 1) + FULL_PERCENT / 2) / FULL_PERCENT
