#include	"dag.h"

/*
  10085  SYNSEM LOCAL CAT VAL SPR FIRST OPT 
  10576  SYNSEM NON-LOCAL SLASH LAST 
  12579  SYNSEM LOCAL CAT VAL SPR FIRST 
  12861  SYNSEM LOCAL CONT MSG 
  13690  SYNSEM LOCAL CAT HEAD MOD FIRST LOCAL 
  15094  SYNSEM LOCAL CAT MC 
  15863  SYNSEM LOCAL CAT VAL SUBJ FIRST OPT 
  15946  SYNSEM LOCAL CAT VAL SUBJ FIRST 
  16647  SYNSEM LOCAL CAT HEAD PRD 
  16882  SYNSEM LOCAL CAT HEAD INV 
  43563  SYNSEM LOCAL CAT HEAD VFORM 
  45221  SYNSEM LOCAL CAT HEAD KEYS KEY 
  */

/*struct qc	extract_qc(struct dg	*d)
{
	struct qc	*q;

	q = malloc(sizeof(struct qc));
}*/

int	quickcheck(struct dg	*a, struct dg	*b)
{
	static int	synsemf, localf, catf, headf, valf, compsf, phf, modf, firstf, init=0;
	struct dg	*synsema, *synsemb, *locala, *localb, *cata, *catb;
	struct dg	*heada, *headb, *vala, *valb, *compsa, *compsb;
	struct dg	*moda, *modb, *modfa, *modfb, *pha, *phb;

	if(!init)
	{
		init = 1;
		synsemf = lookup_fname("SYNSEM");
		localf = lookup_fname("LOCAL");
		catf = lookup_fname("CAT");
		headf = lookup_fname("HEAD");
		valf = lookup_fname("VAL");
		compsf = lookup_fname("COMPS");
		modf = lookup_fname("MOD");
		firstf = lookup_fname("FIRST");
		phf = lookup_fname("POSTHEAD");
	}

	if(!glb(a->xtype, b->xtype))return -1;

	synsema = dg_hop(a, synsemf);
	synsemb = dg_hop(b, synsemf);
	if(synsema && synsemb)
	{
		if(!glb(synsema->xtype, synsemb->xtype))return -1;
		locala = dg_hop(synsema, localf);
		localb = dg_hop(synsemb, localf);
		if(locala && localb)
		{
			cata = dg_hop(locala, catf);
			catb = dg_hop(localb, catf);
			if(cata && catb)
			{
				heada = dg_hop(cata, headf);
				headb = dg_hop(catb, headf);
				if(heada && headb)
				{
					if(!glb(heada->xtype, headb->xtype))return -1;

					moda = dg_hop(heada, modf);
					modb = dg_hop(headb, modf);
					if(moda && modb)
					{
						if(!glb(moda->xtype, modb->xtype))return -1;
						modfa = dg_hop(moda, firstf);
						modfb = dg_hop(modb, firstf);
						if(modfa && modfb)
							if(!glb(modfa->xtype, modfb->xtype))return -1;
					}
				}

				vala = dg_hop(cata, valf);
				valb = dg_hop(catb, valf);
				if(vala && valb)
				{
					compsa = dg_hop(vala, compsf);
					compsb = dg_hop(valb, compsf);
					if(compsa && compsb)
						if(!glb(compsa->xtype, compsb->xtype))return -1;
				}

				pha = dg_hop(cata, phf);
				phb = dg_hop(catb, phf);
				if(pha && phb)
					if(!glb(pha->xtype, phb->xtype))return -1;
			}
		}
	}

	return 0;
}
