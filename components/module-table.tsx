type ModuleRow = {
  path: string
  role: string
}

type ModuleTableProps = {
  title: string
  rows: ModuleRow[]
}

export function ModuleTable({ title, rows }: ModuleTableProps) {
  return (
    <section className="flex flex-col gap-3">
      <h3 className="text-base font-bold">{title}</h3>
      <div className="overflow-hidden rounded-lg border border-border">
        <table className="w-full text-sm">
          <thead>
            <tr className="border-b border-border bg-secondary text-secondary-foreground">
              <th scope="col" className="px-4 py-2.5 text-right font-medium">
                ماژول
              </th>
              <th scope="col" className="px-4 py-2.5 text-right font-medium">
                نقش در معماری
              </th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row) => (
              <tr key={row.path} className="border-b border-border bg-card last:border-b-0">
                <td className="px-4 py-2.5 font-mono text-xs text-muted-foreground" dir="ltr">
                  {row.path}
                </td>
                <td className="px-4 py-2.5 leading-relaxed">{row.role}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  )
}
