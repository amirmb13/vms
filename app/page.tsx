import { PlaneCard } from '@/components/plane-card'
import { ModuleTable } from '@/components/module-table'

const planes = [
  {
    title: 'صفحه کنترل (Control Plane)',
    stack: 'Python · Django · DRF · PostgreSQL · gRPC',
    description:
      'منبع واحد حقیقت برای کاربران، مجوزها (RBAC)، پیکربندی دوربین‌ها، چیدمان‌های شبکه‌ای و لاگ ممیزی. تأیید چهارچشمی برای عملیات حساس و هماهنگ‌سازی ناهمگام با سرورهای ضبط از طریق gRPC و Redis Pub/Sub.',
    modules: [
      'احراز هویت JWT و اتصال به Active Directory',
      'مدیریت دوربین‌ها و پروفایل‌های استریم',
      'جستجوی هوشمند جرم‌شناسی (Forensic)',
      'تقویم شمسی و پیام‌های فارسی در تمام APIها',
    ],
  },
  {
    title: 'صفحه داده (Data Plane)',
    stack: 'Native C++20 · FFmpeg · AVX2/AVX-512 · CUDA',
    description:
      'سرور ضبط با ورودی دوگانه RTSP/ONVIF، آرشیو خام مستقیم با I/O ناهمگام، رله رسانه (Pub/Sub) با بافر حلقوی مشترک و موتور تشخیص حرکت ترکیبی سه‌لایه.',
    modules: [
      'موتور SIMD تفاضل سه‌فریمی (AVX2 و AVX-512)',
      'تفکیک پس‌زمینه MOG2 روی CUDA',
      'ساعت NTP و همگام‌سازی میلی‌ثانیه‌ای',
      'بازیابی آرشیو لبه (ONVIF Profile G)',
    ],
  },
  {
    title: 'کلاینت دسکتاپ ویندوز',
    stack: 'C++ · Qt Quick (QML) · RHI D3D12/Vulkan',
    description:
      'رابط کاربری تمام‌فارسی و راست‌به‌چپ با قلم وزیرمتن؛ شبکه نمایش فوق‌سفارشی با مدل‌های ++C، سوییچ بی‌وقفه کیفیت استریم بر اساس اندازه سلول و پخش همزمان چند دوربین.',
    modules: [
      'موتور شبکه‌ای با ادغام و تغییر اندازه سلول‌ها',
      'پخش همگام سراسری با خط زمانی شمسی',
      'تصویر در تصویر (PiP) با نخ مستقل دیکود',
      'نقشه هوشمند GIS با مخروط دید دوربین‌ها',
    ],
  },
  {
    title: 'موتور هوش مصنوعی لبه',
    stack: 'Python · Docker · ArcFace · gRPC · Shared Memory',
    description:
      'تشخیص چهره و اشیاء در کانتینر ایزوله داکر؛ فریم‌ها بدون هیچ کپی اضافه از حافظه اشتراکی سیستم‌عامل خوانده می‌شوند و تنها توصیف‌گر سبک‌وزن از gRPC عبور می‌کند.',
    modules: [
      'خواندن صفر-کپی فریم‌ها به صورت آرایه NumPy',
      'استنتاج TensorRT FP16 روی GPU',
      'سقوط خودکار به OpenVINO / ONNX INT8 روی CPU',
      'استخراج ویژگی‌های جرم‌شناسی (رنگ لباس، جنسیت)',
    ],
  },
]

const dataPlaneRows = [
  { path: 'server/src/ingest/stream_ingestor.cpp', role: 'دریافت استریم دوگانه RTSP/ONVIF با مهر زمانی NTP' },
  { path: 'server/src/archiver/raw_archiver.cpp', role: 'آرشیو خام مستقیم پکت‌ها با I/O ناهمگام (epoll/IOCP)' },
  { path: 'server/src/relay/media_relay_engine.cpp', role: 'رله رسانه Pub/Sub با بافر حلقوی و مالتی‌کست IGMP' },
  { path: 'server/src/motion/simd_frame_diff.cpp', role: 'موتور تفاضل سه‌فریمی SIMD با مسیرهای AVX2 و AVX-512' },
  { path: 'server/src/motion/motion_engine.cpp', role: 'موتور حرکت سه‌لایه با دیکود NVDEC و تحویل فریم صفر-کپی به هوش مصنوعی' },
  { path: 'server/src/motion/gpu_bg_subtraction.cu', role: 'تفکیک پس‌زمینه MOG2 روی هسته‌های CUDA (لایه ۲)' },
  { path: 'server/src/ipc/shm_frame_writer.cpp', role: 'حلقه حافظه اشتراکی صفر-کپی برای تحویل فریم به هوش مصنوعی' },
  { path: 'server/src/ipc/ai_inference_client.cpp', role: 'کانال سیگنالینگ gRPC دوطرفه با کانتینر هوش مصنوعی' },
  { path: 'server/src/synopsis/time_compressor.cpp', role: 'خلاصه‌سازی ویدئو: فشرده‌سازی ۲۴ ساعت آرشیو در یک کلیپ' },
  { path: 'server/src/edge/edge_retrieval.cpp', role: 'بازیابی و دوخت خودکار حفره‌های آرشیو از کارت SD دوربین' },
]

const clientRows = [
  { path: 'client/src/grid/grid_model.cpp', role: 'موتور شبکه نمایش مبتنی بر QAbstractListModel' },
  { path: 'client/src/stream/stream_controller.cpp', role: 'سوییچ تطبیقی کیفیت استریم بر اساس اندازه سلول' },
  { path: 'client/src/playback/sync_playback.cpp', role: 'پخش همگام سراسری با انتشار مهر زمانی NTP' },
  { path: 'client/src/playback/pip_worker.cpp', role: 'نخ مستقل دیکود برای تصویر در تصویر (PiP)' },
  { path: 'client/src/i18n/shamsi_formatter.cpp', role: 'قالب‌بندی تاریخ شمسی و ارقام فارسی برای QML' },
  { path: 'client/qml/GridEngine.qml', role: 'رابط کاربری شبکه‌ای راست‌به‌چپ با کشیدن و رها کردن' },
  { path: 'client/qml/SmartMap.qml', role: 'نقشه هوشمند با مارکر دوربین و پنجره شناور پخش زنده' },
]

const controlRows = [
  { path: 'backend/apps/accounts', role: 'کاربران، نقش‌ها، احراز هویت JWT و اتصال LDAP' },
  { path: 'backend/apps/cameras', role: 'پیکربندی دوربین‌ها و انتساب به سرورهای ضبط' },
  { path: 'backend/apps/layouts', role: 'ذخیره چیدمان‌های شبکه‌ای به صورت JSON اعتبارسنجی‌شده' },
  { path: 'backend/apps/forensic', role: 'جستجوی هوشمند جرم‌شناسی روی متادیتای هوش مصنوعی' },
  { path: 'backend/apps/audit', role: 'لاگ ممیزی با تاریخ شمسی و تأیید چهارچشمی' },
  { path: 'backend/apps/orchestrator', role: 'ارکستراسیون gRPC و Redis Pub/Sub با سرورهای ++C' },
  { path: 'ai_engine/main.py', role: 'سرویس استنتاج ArcFace با خواندن صفر-کپی حافظه اشتراکی' },
]

export default function Page() {
  return (
    <main className="mx-auto flex min-h-screen w-full max-w-5xl flex-col gap-12 px-6 py-12">
      <header className="flex flex-col gap-4">
        <p className="font-mono text-xs text-primary" dir="ltr">
          Enterprise VMS — 10,000+ Cameras
        </p>
        <h1 className="text-3xl font-bold leading-relaxed text-balance md:text-4xl">
          سامانه مدیریت تصاویر سازمانی
        </h1>
        <p className="max-w-3xl text-base leading-relaxed text-muted-foreground text-pretty">
          معماری چهار‌لایه با جداسازی سخت‌گیرانه صفحه کنترل و صفحه داده: مدیریت و پایگاه داده در
          Django، پردازش ویدئو در ++C بومی، رابط دسکتاپ در Qt Quick با رندر سخت‌افزاری، و هوش
          مصنوعی لبه در کانتینر داکر با حافظه اشتراکی صفر-کپی. تمام رابط کاربری فارسی، راست‌به‌چپ و
          مبتنی بر تقویم شمسی است.
        </p>
        <p className="text-sm text-muted-foreground">
          این صفحه، سند وضعیت پروژه است؛ کد اصلی در پوشه‌های{' '}
          <code className="rounded bg-secondary px-1.5 py-0.5 font-mono text-xs" dir="ltr">
            backend/
          </code>{' '}
          و{' '}
          <code className="rounded bg-secondary px-1.5 py-0.5 font-mono text-xs" dir="ltr">
            server/
          </code>{' '}
          و{' '}
          <code className="rounded bg-secondary px-1.5 py-0.5 font-mono text-xs" dir="ltr">
            client/
          </code>{' '}
          و{' '}
          <code className="rounded bg-secondary px-1.5 py-0.5 font-mono text-xs" dir="ltr">
            ai_engine/
          </code>{' '}
          قرار دارد.
        </p>
      </header>

      <section aria-labelledby="planes-heading" className="flex flex-col gap-5">
        <h2 id="planes-heading" className="text-xl font-bold">
          چهار لایه معماری
        </h2>
        <div className="grid gap-5 md:grid-cols-2">
          {planes.map((plane) => (
            <PlaneCard key={plane.title} {...plane} />
          ))}
        </div>
      </section>

      <section aria-labelledby="modules-heading" className="flex flex-col gap-6">
        <h2 id="modules-heading" className="text-xl font-bold">
          فهرست ماژول‌های پیاده‌سازی‌شده
        </h2>
        <ModuleTable title="سرور ضبط و رله رسانه (++C)" rows={dataPlaneRows} />
        <ModuleTable title="کلاینت دسکتاپ (Qt Quick / QML)" rows={clientRows} />
        <ModuleTable title="بک‌اند مدیریتی و هوش مصنوعی (Django / Python)" rows={controlRows} />
      </section>

      <footer className="border-t border-border pt-6 text-sm text-muted-foreground">
        <p className="leading-relaxed">
          ساخت سرور: <code className="font-mono text-xs" dir="ltr">cmake -S server -B build</code>{' '}
          — ساخت کلاینت: <code className="font-mono text-xs" dir="ltr">cmake -S client -B build-client</code>{' '}
          — اجرای کامل: <code className="font-mono text-xs" dir="ltr">docker compose -f docker/docker-compose.yml up</code>
        </p>
      </footer>
    </main>
  )
}
