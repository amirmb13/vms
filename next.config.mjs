/** @type {import('next').NextConfig} */
const nextConfig = {
  typescript: {
    ignoreBuildErrors: true,
  },
  images: {
    unoptimized: true,
  },
  // TEMP (v0 verification): allow trailing-slash paths to reach the Django proxy routes.
  skipTrailingSlashRedirect: true,
}

export default nextConfig
