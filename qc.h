#ifndef	QC_H
#define QC_H

struct qc	*extract_qc(struct dg	*d);
struct qc	*extract_qc_arg(struct dg	*d, int	arg);
int			quickcheck(struct qc	*a, struct qc	*b);
int			equiv_quickcheck(struct qc	*a, struct qc	*b, int *Fw, int *Bk);
struct qc	*copy_qc(struct qc	*in);
struct type	*qc_type_n(struct qc	*in, int	n);

#ifdef	CONF_H
void	gqc_count_failure(struct path	path);
void	gqc_result();
#endif

extern int __qc_use_heap;

#endif
