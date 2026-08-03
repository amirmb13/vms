#pragma once
// =============================================================================
// ShamsiFormatter — Solar Hijri (Shamsi) date/time rendering for QML.
//
// Internal timestamps are ALWAYS UTC (NTP-disciplined, microseconds).
// Conversion to Shamsi + Persian digits happens exclusively at the UI edge:
//   shamsi.nowShamsi()            -> "۱۴۰۵/۰۴/۳۰ - ۱۸:۵۱:۱۵"
//   shamsi.toShamsi(utcUs)        -> same format for a given UTC microsecond
//   shamsi.toShamsiShort(utcUs)   -> "۱۸:۵۱:۱۵" (timeline tick labels)
//
// Timezone: Iran Standard Time (IRST, UTC+3:30). IRDT handling is delegated
// to QTimeZone("Asia/Tehran") so historical DST rules stay correct.
// =============================================================================
#include <QObject>
#include <QDateTime>
#include <QString>
#include <QTimeZone>

namespace vms {

class ShamsiFormatter : public QObject {
    Q_OBJECT

public:
    explicit ShamsiFormatter(QObject* parent = nullptr);

    // "۱۴۰۵/۰۴/۳۰ - ۱۸:۵۱:۱۵" — live header clock.
    Q_INVOKABLE QString nowShamsi() const;

    // Full Shamsi stamp for a UTC microsecond timestamp (video overlay, audit).
    Q_INVOKABLE QString toShamsi(qulonglong utcMicroseconds) const;

    // Time-only ("۱۸:۵۱:۱۵") for dense timeline tick labels.
    Q_INVOKABLE QString toShamsiShort(qulonglong utcMicroseconds) const;

    // Date-only ("۱۴۰۵/۰۴/۳۰") for forensic search filters.
    Q_INVOKABLE QString toShamsiDate(qulonglong utcMicroseconds) const;

    // Western digits -> Persian digits ("2024" -> "۲۰۲۴") for arbitrary labels.
    Q_INVOKABLE QString toPersianDigits(const QString& text) const;

    // Reverse mapping: a Shamsi date picked in the UI -> UTC microseconds
    // (start of that Shamsi day in Asia/Tehran), for forensic API queries.
    Q_INVOKABLE qulonglong shamsiDateToUtcUs(int jy, int jm, int jd) const;

    // Exposed for QML date pickers.
    Q_INVOKABLE int shamsiYear(qulonglong utcMicroseconds) const;
    Q_INVOKABLE int shamsiMonth(qulonglong utcMicroseconds) const;
    Q_INVOKABLE int shamsiDay(qulonglong utcMicroseconds) const;
    Q_INVOKABLE QString shamsiMonthName(int jm) const;  // "تیر", "مرداد", ...

    // --- Pure conversion core (also unit-testable without Qt event loop) ----
    struct JalaliDate { int year; int month; int day; };
    static JalaliDate gregorianToJalali(int gy, int gm, int gd);
    static void jalaliToGregorian(int jy, int jm, int jd,
                                  int& gy, int& gm, int& gd);

private:
    QDateTime tehranTime(qulonglong utcMicroseconds) const;
};

}  // namespace vms
