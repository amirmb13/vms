// TEMP (v0 verification only): proxies /admin/* to the local Django dev
// server so the admin redesign can be previewed. Will be removed.
import type { NextRequest } from "next/server"

const DJANGO = "http://127.0.0.1:8300"

async function proxy(req: NextRequest) {
  const url = new URL(req.url)
  const target = `${DJANGO}${url.pathname}${url.search}`
  const headers = new Headers(req.headers)
  headers.set("host", "127.0.0.1:8300")
  headers.delete("accept-encoding")
  const res = await fetch(target, {
    method: req.method,
    headers,
    body: req.method === "GET" || req.method === "HEAD" ? undefined : await req.arrayBuffer(),
    redirect: "manual",
  })
  const out = new Headers(res.headers)
  out.delete("content-encoding")
  out.delete("content-length")
  return new Response(res.body, { status: res.status, headers: out })
}

export const GET = proxy
export const POST = proxy
export const HEAD = proxy
