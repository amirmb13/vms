#include "i18n/shamsi_formatter.h"

#include <QTimeZone>

namespace vms {
namespace {

// Persian (Extended Arabic-Indic) digits U+06F0..U+06F9.
const QChar kPersianDigits[10] = {
    QChar(0x06F0), QChar(0x06F1), QChar(0x06F2), QChar(0x06F3), QChar(0x06F4),
    QChar(0x06F5), QChar(0x06F6), QChar(0x06F7), QChar(0x06F8), QChar(0x06F9)};

QString persian(int value, int pad) {
    QString western = QStringLiteral("%1").arg(value, pad, 10, QChar('0'));
    QString out;
    out.reserve(western.size());
    for (QChar c : western)
        out.append(c.isDigit() ? kPersianDigits[c.digitValue()] : c);
    return out;
}

const char* kMonthNamesFa[12] = {
    "فروردین", "اردیبهشت", "خرداد", "تیر", "مرداد", "شهریور",
    "مهر", "آبان", "آذر", "دی", "بهمن", "اسفند"};

}  // namespace

ShamsiFormatter::ShamsiFormatter(QObject* parent) : QObject(parent) {}

// --- Jalali conversion core (Khayyam/Birashk-compatible arithmetic) ---------
ShamsiFormatter::JalaliDate ShamsiFormatter::gregorianToJalali(int gy, int gm,
                                                               int gd) {
    static const int g_days_in_month[12] = {31, 28, 31, 30, 31, 30,
                                            31, 31, 30, 31, 30, 31};
    int gy2 = (gm > 2) ? (gy + 1) : gy;
    long days = 355666L + (365L * gy) + ((gy2 + 3) / 4) - ((gy2 + 99) / 100) +
                ((gy2 + 399) / 400) + gd;
    for (int i = 0; i < gm - 1; ++i) days += g_days_in_month[i];

    int jy = -1595 + static_cast<int>(33 * (days / 12053L));
    days %= 12053L;
    jy += 4 * static_cast<int>(days / 1461L);
    days %= 1461L;
    if (days > 365) {
        jy += static_cast<int>((days - 1) / 365L);
        days = (days - 1) % 365L;
    }
    int jm, jd;
    if (days < 186) {
        jm = 1 + static_cast<int>(days / 31);
        jd = 1 + static_cast<int>(days % 31);
    } else {
        jm = 7 + static_cast<int>((days - 186) / 30);
        jd = 1 + static_cast<int>((days - 186) % 30);
    }
    return {jy, jm, jd};
}

void ShamsiFormatter::jalaliToGregorian(int jy, int jm, int jd, int& gy,
                                        int& gm, int& gd) {
    jy += 1595;
    long days = -355668L + (365L * jy) + ((jy / 33) * 8) +
                (((jy % 33) + 3) / 4) + jd;
    days += (jm < 7) ? (jm - 1) * 31L : ((jm - 7) * 30L + 186L);

    gy = 400 * static_cast<int>(days / 146097L);
    days %= 146097L;
    if (days > 36524) {
        gy += 100 * static_cast<int>(--days / 36524L);
        days %= 36524L;
        if (days >= 365) ++days;
    }
    gy += 4 * static_cast<int>(days / 1461L);
    days %= 1461L;
    if (days > 365) {
        gy += static_cast<int>((days - 1) / 365L);
        days = (days - 1) % 365L;
    }
    gd = static_cast<int>(days + 1);
    const bool leap = ((gy % 4 == 0) && (gy % 100 != 0)) || (gy % 400 == 0);
    const int sal_a[13] = {0,  31, leap ? 29 : 28, 31, 30, 31, 30,
                           31, 31, 30, 31, 30, 31};
    for (gm = 1; gm <= 12 && gd > sal_a[gm]; ++gm) gd -= sal_a[gm];
}

// --- Qt-facing API -----------------------------------------------------------
QDateTime ShamsiFormatter::tehranTime(qulonglong utcMicroseconds) const {
    const QDateTime utc = QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(utcMicroseconds / 1000), QTimeZone::utc());
    return utc.toTimeZone(QTimeZone("Asia/Tehran"));
}

QString ShamsiFormatter::nowShamsi() const {
    return toShamsi(static_cast<qulonglong>(
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL);
}

QString ShamsiFormatter::toShamsi(qulonglong utcMicroseconds) const {
    const QDateTime local = tehranTime(utcMicroseconds);
    const QDate d = local.date();
    const QTime t = local.time();
    const JalaliDate j = gregorianToJalali(d.year(), d.month(), d.day());
    // ۱۴۰۵/۰۴/۳۰ - ۱۸:۵۱:۱۵
    return QStringLiteral("%1/%2/%3 - %4:%5:%6")
        .arg(persian(j.year, 4), persian(j.month, 2), persian(j.day, 2),
             persian(t.hour(), 2), persian(t.minute(), 2),
             persian(t.second(), 2));
}

QString ShamsiFormatter::toShamsiShort(qulonglong utcMicroseconds) const {
    const QTime t = tehranTime(utcMicroseconds).time();
    return QStringLiteral("%1:%2:%3").arg(persian(t.hour(), 2),
                                          persian(t.minute(), 2),
                                          persian(t.second(), 2));
}

QString ShamsiFormatter::toShamsiDate(qulonglong utcMicroseconds) const {
    const QDate d = tehranTime(utcMicroseconds).date();
    const JalaliDate j = gregorianToJalali(d.year(), d.month(), d.day());
    return QStringLiteral("%1/%2/%3").arg(persian(j.year, 4),
                                          persian(j.month, 2),
                                          persian(j.day, 2));
}

QString ShamsiFormatter::toPersianDigits(const QString& text) const {
    QString out;
    out.reserve(text.size());
    for (QChar c : text)
        out.append(c.isDigit() ? kPersianDigits[c.digitValue()] : c);
    return out;
}

qulonglong ShamsiFormatter::shamsiDateToUtcUs(int jy, int jm, int jd) const {
    int gy, gm, gd;
    jalaliToGregorian(jy, jm, jd, gy, gm, gd);
    const QDateTime start(QDate(gy, gm, gd), QTime(0, 0),
                          QTimeZone("Asia/Tehran"));
    return static_cast<qulonglong>(start.toUTC().toMSecsSinceEpoch()) * 1000ULL;
}

int ShamsiFormatter::shamsiYear(qulonglong us) const {
    const QDate d = tehranTime(us).date();
    return gregorianToJalali(d.year(), d.month(), d.day()).year;
}
int ShamsiFormatter::shamsiMonth(qulonglong us) const {
    const QDate d = tehranTime(us).date();
    return gregorianToJalali(d.year(), d.month(), d.day()).month;
}
int ShamsiFormatter::shamsiDay(qulonglong us) const {
    const QDate d = tehranTime(us).date();
    return gregorianToJalali(d.year(), d.month(), d.day()).day;
}

QString ShamsiFormatter::shamsiMonthName(int jm) const {
    if (jm < 1 || jm > 12) return {};
    return QString::fromUtf8(kMonthNamesFa[jm - 1]);
}

}  // namespace vms
