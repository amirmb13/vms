type PlaneCardProps = {
  title: string
  stack: string
  description: string
  modules: string[]
}

export function PlaneCard({ title, stack, description, modules }: PlaneCardProps) {
  return (
    <article className="flex flex-col gap-4 rounded-lg border border-border bg-card p-6 text-card-foreground">
      <header className="flex flex-col gap-1">
        <h3 className="text-lg font-bold text-balance">{title}</h3>
        <p className="font-mono text-xs text-primary" dir="ltr">
          {stack}
        </p>
      </header>
      <p className="text-sm leading-relaxed text-muted-foreground text-pretty">{description}</p>
      <ul className="mt-auto flex flex-col gap-2">
        {modules.map((m) => (
          <li key={m} className="flex items-center gap-2 text-sm">
            <span aria-hidden="true" className="size-1.5 shrink-0 rounded-full bg-primary" />
            {m}
          </li>
        ))}
      </ul>
    </article>
  )
}
