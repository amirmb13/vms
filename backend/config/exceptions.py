"""Custom DRF exception handler — ALL API errors are returned in Farsi."""
from rest_framework.views import exception_handler

FARSI_STATUS_MESSAGES = {
    400: "درخواست نامعتبر است. لطفاً اطلاعات ورودی را بررسی کنید.",
    401: "احراز هویت انجام نشده است. لطفاً وارد حساب کاربری خود شوید.",
    403: "شما مجوز دسترسی به این بخش را ندارید.",
    404: "منبع درخواستی یافت نشد.",
    409: "تعارض در داده‌ها؛ عملیات قابل انجام نیست.",
    423: "این عملیات نیازمند تأیید همزمان دو مدیر است (احراز چهارچشمی).",
    429: "تعداد درخواست‌ها بیش از حد مجاز است. لطفاً کمی صبر کنید.",
    500: "خطای داخلی سرور رخ داده است. با مدیر سامانه تماس بگیرید.",
    503: "سرویس در حال حاضر در دسترس نیست.",
}


def farsi_exception_handler(exc, context):
    response = exception_handler(exc, context)
    if response is not None:
        response.data = {
            "error_fa": FARSI_STATUS_MESSAGES.get(
                response.status_code, "خطای ناشناخته‌ای رخ داده است."
            ),
            "detail": response.data,
            "status": response.status_code,
        }
    return response
